/*
 * File: giomap_linux.c
 *
 * Linux harness for a GIO mapper. Common code is held in
 * ../../mapper/common/gio/giomap.c
 *
 * Provided interfaces:
 *
 * Mapper use:
 *	giomap_OS_init		- called as part of initialization sequence
 *	giomap_OS_bind_done	- called as last step in CHANNEL_BOUND process
 *	giomap_OS_io_done	- called on completion of data transfer
 *	giomap_OS_abort_ack	- called on completion of udi_gio_abort_req()
 *	giomap_OS_channel_event	- called when a channel event occurs
 *	giomap_OS_unbind_done	- called when UDI_DMGMT_UNBIND occurs
 *	giomap_OS_event		- called on receipt of udi_gio_event_ind
 *
 * Linux DDK use:
 *	giomap_open
 *	giomap_release
 *	giomap_ioctl
 *	giomap_read   (giomap_biostart)
 *	giomap_write  (giomap_biostart)
 * ============================================================================
 */
#if LINUX_GIO_DEBUG
#define FUNC_TRACE(x) printk("enter: %s\n", #x);
#else
#define FUNC_TRACE(x) do { } while (0);
#endif

/* From common/giomap.c
 *
 *  Required OS-specific routines:
 *      giomap_OS_init  
 *          initialise OS-specific members of the region data area.
 *          This is a non-blocking synchronous routine.
 *      giomap_OS_deinit
 *          release any OS-specific members of the region data area
 *          which were allocated by giomap_OS_init (e.g. MUTEXs).
 *          This is a non-blocking routine.
 *      giomap_OS_bind_done
 *          Called when the GIO bind has completed. The OS code
 *          should perform any initialisation required and then
 *          call udi_channel_event_complete with the passed
 *          parameters
 *      giomap_OS_unbind_done
 *          Called when the UDI_DMGMT_UNBIND operation has removed
 *          the parent-bind channel. The OS-specific code should
 *          release any bindings instantiated by giomap_OS_bind_done
 *      giomap_OS_io_done
 *          called on completion of a udi_gio_xfer_req for both
 *          successful and unsuccessful completion cases
 *      giomap_OS_abort_ack
 *          called on completion of a udi_gio_abort_req
 *      giomap_OS_channel_event
 *          called on receipt of a udi_channel_event_ind
 *      giomap_OS_event
 *          called on receipt of a udi_gio_event_ind 
 * --------------------------------------------------------------------
 *      giomap_OS_Alloc_resources
 *          allocate all internal resources required by the mapper.
 *          This will be called once the initial MA binding is
 *          established but before the meta-specific binding has
 *          been done. This is an asynchronous routine which must
 *          call giomap_Resources_Alloced on completion. 
 *      giomap_OS_Free_resources 
 *          free-up all internal resources allocated by
 *          giomap_OS_Alloc_resources. This will be called prior
 *          to the MA-specific unbind completing.
 *          This routine is synchronous.
 * --------------------------------------------------------------------
 *      
 *  Provided OS-specific interfaces:
 *      giomap_Resources_Alloced
 *          Called when all OS-specific resources (control blocks,
 *          buffers etc.) have been allocated. 
 * ========================================================================
 */

/*
 * $Copyright udi_reference:
 * 
 * 
 *    Copyright (c) 1995-2001; Compaq Computer Corporation; Hewlett-Packard
 *    Company; Interphase Corporation; The Santa Cruz Operation, Inc;
 *    Software Technologies Group, Inc; and Sun Microsystems, Inc
 *    (collectively, the "Copyright Holders").  All rights reserved.
 * 
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the conditions are met:
 * 
 *            Redistributions of source code must retain the above
 *            copyright notice, this list of conditions and the following
 *            disclaimer.
 * 
 *            Redistributions in binary form must reproduce the above
 *            copyright notice, this list of conditions and the following
 *            disclaimers in the documentation and/or other materials
 *            provided with the distribution.
 * 
 *            Neither the name of Project UDI nor the names of its
 *            contributors may be used to endorse or promote products
 *            derived from this software without specific prior written
 *            permission.
 * 
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *    "AS IS," AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *    HOLDERS OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *    OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *    DAMAGE.
 * 
 *    THIS SOFTWARE IS BASED ON SOURCE CODE PROVIDED AS A SAMPLE REFERENCE
 *    IMPLEMENTATION FOR VERSION 1.01 OF THE UDI CORE SPECIFICATION AND/OR
 *    RELATED UDI SPECIFICATIONS. USE OF THIS SOFTWARE DOES NOT IN AND OF
 *    ITSELF CONSTITUTE CONFORMANCE WITH THIS OR ANY OTHER VERSION OF ANY
 *    UDI SPECIFICATION.
 * 
 * 
 * $
 */                         

#include "udi_osdep_opaquedefs.h"
#include <udi_env.h>
#include <udi.h>
#include <udi_core/udi_gio.h>

#ifdef LINUX_GIO_DEBUG
#define DEBUGPRINT( x ) _OSDEP_PRINTF x
#else
#define DEBUGPRINT( x ) do { } while (0)
#endif

/* Linux DDI */
#ifndef __NO_VERSION__
#define __NO_VERSION__
#endif
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/fs.h>

/*
 * Use devfs headers only if devfs is actually enabled, otherwise use
 * our own stubs, so we don't rely on the devfs headers.
 */
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#else
#define devfs_register_chrdev(maj,name,ops)	register_chrdev(maj,name,ops)
#define devfs_unregister_chrdev(maj,name)	unregister_chrdev(maj,name)
#endif

#include "giomap_linux.h"

#define MAPPER_NAME "udiMgio"				/* O.S. specific */

/*
 * Global driver-specific data [for OS use only]
 */

int	nGIOmappers;			/* # of GIO instances */

#if UNIXWARE
bcb_t	*giomap_bcb;			/* Default BCB */
#endif

/*
 * Forward declarations
 */
struct giomap_region_data;
struct giomap_init_data;


STATIC void *	giomap_attach(const char *, udi_boolean_t );
STATIC int	giomap_detach(void *);

/*
 * Data structure used to track the instance <-> region local data mapping.
 * This is needed because of the interaction between the UDI bind sequence
 * and the UnixWare device enumeration (CFG_xxx) sequences. The first device
 * will not be made available to the O.S. until the first _udi_MA_bind has
 * completed. At this time _udi_MA_local_data will return the data reference.
 * However, all of the drvmap'd resmgr entries will then be enumerated using
 * CFG_ADD and because we are still in the middle of enumerating the UDI
 * instances, only the first entry will be found.
 * To work around this problem we indirect the <idatap> parameters to refer
 * to a giomap_init_data element which contains the instance number and the
 * rdata reference (if non-NULL). On device open() we can then get the correct
 * data reference as the UDI enumeration cycle will have been completed by this
 * time
 */

typedef struct giomap_init_data {
	udi_ubit32_t	instance;	/* UDI instance */
	struct giomap_region_data *rdata;	/* <rdata> reference */
} giomap_init_data_t ;

typedef struct {
	udi_queue_t	Q;
	const char	*modname;
	int		nrefs;
	void		*drvinfop;
} giomap_mod_t ;

udi_queue_t	giomap_mod_Q;		/* Queue of bound driver modnames */

#if UNIXWARE
rm_key_t	gio_key;		/* Global shared data */

STATIC void	giomap_start(void);
STATIC int	giomap_devinfo(void *, channel_t , di_parm_t , void *);
STATIC int	giomap_config(cfg_func_t , void *, rm_key_t );
STATIC int	giomap_open(void *, channel_t *, int, cred_t *, queue_t *);
STATIC int	giomap_close(void *, channel_t , int, cred_t *, queue_t *);
STATIC void	giomap_biostart(void *, channel_t , buf_t *);
STATIC int	giomap_ioctl(void *, channel_t , int, void *, int, cred_t *,
			int *);
STATIC int	giomap_drvctl(void *, channel_t , int, void * );

STATIC drvops_t giomap_drvops = {
	giomap_config,		/* config */
	giomap_open,		/* open */
	giomap_close,		/* close */
	giomap_devinfo,		/* devinfo */
	giomap_biostart,	/* biostart */
	giomap_ioctl,		/* ioctl */
	giomap_drvctl,		/* drvctl */
	NULL			/* mmap */
};

STATIC drvinfo_t giomap_drvinfo = {
	&giomap_drvops, MAPPER_NAME, D_RANDOM|D_MP, NULL, 1
};
#else
/* LINUX Not all of these are implemented for a gio device. */
#if 0
STATIC loff_t giomap_llseek (struct file *, loff_t, int);
STATIC int giomap_readdir (struct file *, void *, filldir_t);
STATIC unsigned int giomap_poll (struct file *, struct poll_table_struct *);
STATIC int giomap_mmap (struct file *, struct vm_area_struct *);
STATIC int giomap_flush (struct file *);
STATIC int giomap_fsync (struct file *, struct dentry *);
STATIC int giomap_fasync (int, struct file *, int);
STATIC int giomap_check_media_change (kdev_t dev);
STATIC int giomap_revalidate (kdev_t dev);
STATIC int giomap_lock (struct file *, int, struct file_lock *);
#endif
STATIC ssize_t giomap_read (struct file *, char *, size_t, loff_t *);
STATIC ssize_t giomap_write (struct file *, const char *, size_t, loff_t *);
STATIC int giomap_ioctl (struct inode *, struct file *, unsigned int, unsigned long);
STATIC int giomap_open (struct inode *, struct file *);
STATIC int giomap_release (struct inode *, struct file *);

typedef struct {
/* public */
	struct file_operations drv_ops;
/*
 * Notice how this struct is INHERITED from file_operations... this is so we
 * can relate per-device data without indexing into a list based on the
 * MAJOR number in the /dev/ node.
 * This makes us kernel version dependent... this code must be recompiled
 * for each version of the kernel it is expected to work with.
 */
 
	char *drv_name;
	char *drv_str;

/* private */
#ifdef CONFIG_DEVFS_FS
	devfs_handle_t de;		/* devfs handle */
#endif
	unsigned int major;
	struct giomap_init_data		*per_device_data; /* aka idatap */
	struct giomap_region_data	*per_device_rdata;
} drvinfo_t;

/* This copyin and copyout should NOT be used in the read/write
 * routines since the 2.1 kernel already checks memory space
 * validity for us.
 */
STATIC int copyin(void *usrc, void *kdst, size_t cn)
{
FUNC_TRACE(copyin);
	if (copy_from_user(kdst, usrc, cn) == 0)
		return 0;
	else
		return -1;
}

STATIC int copyout(void *ksrc, void *udst, size_t cn)
{
FUNC_TRACE(copyout);
	if (copy_to_user(udst, ksrc, cn) == 0)
		return 0;
	else
		return -1;
}

enum enum_cfg_func_t
{
	CFG_ADD = 0,
	CFG_REMOVE,
	CFG_SUSPEND,
	CFG_RESUME
};
typedef enum enum_cfg_func_t cfg_func_t;

/* Helper functions for DDI.
 */
STATIC int  giomap_config(cfg_func_t func, drvinfo_t *drvinfo);
STATIC int drv_detach(drvinfo_t *drvinfo);

#define DEV_NODE_NAME_MAXLEN 128
#define DEV_NODE_DEFAULT_MODE 0x0664

#ifdef DEBUG
STATIC void dump_perdevdata(struct giomap_init_data *per_device_data);
#endif

STATIC int drv_attach(drvinfo_t *drvinfo)
{
	char nodename[DEV_NODE_NAME_MAXLEN];
	int result;

	FUNC_TRACE(drv_attach);

	result = giomap_config(CFG_ADD,	drvinfo);
	/*
	 * Possible results we expect are:
	 * ENODEV, 0, EOPNOTSUPP
	 */
	if (result != 0)
		return -ENODEV;
#ifdef DEBUG
	dump_perdevdata(drvinfo->per_device_data);
	printk("register_chrdev(0,'%s',%p) = ", drvinfo->drv_name, &drvinfo->drv_ops);
#endif

	result = devfs_register_chrdev(0, drvinfo->drv_name, &drvinfo->drv_ops);

#ifdef DEBUG
	printf(" %08X\n", result);
#endif
	if (result < 0) {
		drvinfo->major = 0;
		_OSDEP_PRINTF("udiM_gio: failed to register_chrdev:%d\n", result);
		drv_detach(drvinfo);
		return result;
	}
	drvinfo->major = result; /* dynamic */

 	udi_snprintf(nodename, sizeof(nodename), "%s%d", drvinfo->drv_name, 0);
#ifdef CONFIG_DEVFS_FS
	drvinfo->de = devfs_register(NULL, nodename, DEVFS_FL_NONE, 
				     drvinfo->major, 0, 
				     S_IFCHR | S_IRUGO | S_IWUGO,
				     &drvinfo->drv_ops, NULL);
		
	if (result < 0) {
		_OSDEP_PRINTF("udiM_gio: Cannot make the device node.\n"
			      "udiM_gio: Please create it with '/bin/mknod %s c %d 0'\n",
			nodename, drvinfo->major);
	
	}
#else
	_OSDEP_PRINTF("udiM_gio: Create device node with '/bin/mknod /dev/%s c %d 0'\n",
		      nodename, drvinfo->major);
#endif	
	return 0;
}

STATIC int drv_detach(drvinfo_t *drvinfo)
{
	int result;
	FUNC_TRACE(drv_detach);

#ifdef CONFIG_DEVFS_FS
	if (drvinfo->de) 
		devfs_unregister(drvinfo->de);
#endif

	if (drvinfo->major != 0)
		result = devfs_unregister_chrdev(drvinfo->major,
				drvinfo->drv_name);
	
	result = giomap_config(CFG_REMOVE, drvinfo);
	return result;
}

STATIC const
drvinfo_t giomap_drvinfo = {
/*
STATIC const
struct file_operations giomap_drvops =
*/
	{
#if LINUX_KERNEL_VERSION >= KERNEL_VERSION(2,3,0)
		owner:		THIS_MODULE,		/* owner module */
#endif
		open:		giomap_open,
		release:	giomap_release,
		ioctl:		giomap_ioctl,
		read:		giomap_read,
		write:		giomap_write,
		llseek:		NULL,
		/*
		 * Things that could be here:
		 * llseek, readdir, poll, mmap, flush, fsync, fasync
		 * check_media_change, revalidate, lock
		 */
	}
    	,
	MAPPER_NAME,			/* drv_name */
	"A UDI GIO Mapper Device",	/* drv_str */
	0,				/* major */
	NULL,				/* per_device_data */
};

/*
(file*)->private_data; //per file*, ie. per open.
(file*)->f_op; // per device, ie. per instance.
// Globals are per kernel module.
*/

typedef int channel_t;
typedef giomap_buf_t buf_t;

STATIC void *
giomap_get_per_device_data(struct file *filp)
{
	return ((drvinfo_t*)(filp->f_op))->per_device_data;
}

#if 0
STATIC void*
giomap_get_per_open_data(struct file *filp)
{
_OSDEP_PRINTF("get_per_open_data is probably NULL!\n");
	return filp->private_data;
}
#endif

STATIC channel_t
giomap_get_channel(struct file *filp)
{
	channel_t channel;
	int major, minor;

	major = MAJOR(filp->f_dentry->d_inode->i_rdev);
	minor = MINOR(filp->f_dentry->d_inode->i_rdev);

	channel = major << 16 | minor;
	return channel;
}


#endif


/*
 * giomap_attach:
 * -------------
 * Construct a drvinfo_t structure which can be drv_attach()d by the calling
 * driver [the udi_glue code]. This is needed to allow the CFG_xxx resmgr
 * initiated calls to be correctly despatched to both the wrapper driver and
 * this mapper.
 * Calling sequence for CFG_ADD is:
 *		wrapper.cfg -> giomap_config
 * The wrapper.cfg is responsible for _udi_driver_load()ing the parent driver,
 * while the giomap_config loads the mapper and completes the _udi_MA_bind()
 * sequence making the GIO instance available for access.
 *
 * Returns:
 *	reference to the newly created drvinfo_t structure.
 */
STATIC void *
giomap_attach(const char *modname, udi_boolean_t is_random)
{
	drvinfo_t	*mydrvinfo;
	char		*myname;

	FUNC_TRACE(giomap_attach);

	mydrvinfo = (drvinfo_t *) _OSDEP_MEM_ALLOC( sizeof( drvinfo_t ), 0, 0 );
	myname = (char *) _OSDEP_MEM_ALLOC( udi_strlen(modname) + 1, 0, 0 );
		
	if ( mydrvinfo == NULL || myname == NULL ) {
		if ( mydrvinfo ) {
			_OSDEP_MEM_FREE(mydrvinfo);
		}
		if (myname) {
			_OSDEP_MEM_FREE(myname);
		}
		return NULL;
	} else {
		/* Notice, we copy these in here because we're assuming that
		 * the linux f_ops struct doesn't change in size. Yes, this makes
		 * us kernel version dependent! But, other than setting up a 
		 * list of global f_ops (*ACK!*), how else can we get per-device data
		 * attached onto the file* or f_ops?!! file->private_data is a
		 * per-open field, so we sure cant use that.
		 */
		mydrvinfo->drv_ops = giomap_drvinfo.drv_ops;
		udi_strcpy(myname, modname);
		mydrvinfo->drv_name = myname;
#if UNIXWARE
		mydrvinfo->drv_flags = giomap_drvinfo.drv_flags;
		if ( is_random ) {
			mydrvinfo->drv_flags |= D_RANDOM;
		} else {
			mydrvinfo->drv_flags &= ~D_RANDOM;
		}
#endif
		mydrvinfo->drv_str = giomap_drvinfo.drv_str;
#if UNIXWARE
		mydrvinfo->drv_maxchan = giomap_drvinfo.drv_maxchan;
#endif
		
		return mydrvinfo;
	}
}


/*
 * giomap_detach:
 * -------------
 * Release the previousy allocated drvinfo_t structure. Called after the
 * wrapper driver has drv_detach'd itself.
 */
STATIC int
giomap_detach(void *arg)
{
	drvinfo_t	*mydrvinfo = (drvinfo_t *)arg;

	FUNC_TRACE(giomap_detach);

	if ( mydrvinfo ) {

		_OSDEP_MEM_FREE( (void *)(mydrvinfo->drv_name) );
		_OSDEP_MEM_FREE( mydrvinfo );
	}

	return 0;
}

/*
 * giomap_start:
 * ------------
 * Allocate a default BCB which can be used for sending any data to devices
 * which do not require a more restrictive set of constraints than the OS
 * default.
 */
STATIC void
giomap_start(void)
{
	FUNC_TRACE(giomap_start);
#if UNIXWARE
	physreq_t	*preqp;

	giomap_bcb = (bcb_t *)_OSDEP_MEM_ALLOC( sizeof( bcb_t ), 0, 0 );
	if ( giomap_bcb == NULL )
		return;
	preqp = physreq_alloc(KM_NOSLEEP);
	if ( preqp == NULL ) {
		_OSDEP_MEM_FREE( giomap_bcb );
		giomap_bcb = NULL;
		return;
	}
	
	giomap_bcb->bcb_addrtypes = BA_KVIRT;
	giomap_bcb->bcb_flags = BCB_ONE_PIECE | BCB_SYNCHRONOUS;
	giomap_bcb->bcb_max_xfer = 0;		/* Unlimited */
	giomap_bcb->bcb_granularity = 1;
	giomap_bcb->bcb_physreqp = preqp;

	preqp->phys_align = 1;
	preqp->phys_boundary = 0;
	preqp->phys_dmasize = 0;
	preqp->phys_dmasize = 0;
	preqp->phys_max_scgth = 0;
	preqp->phys_flags = 0;

	if ( !bcb_prep(giomap_bcb, KM_NOSLEEP) ) {
		physreq_free(preqp);
		_OSDEP_MEM_FREE(giomap_bcb);
		giomap_bcb = NULL;
		return;
	}
#endif /*UNIXWARE*/		
}

/*
 * config entry point
 */
STATIC int
giomap_config(cfg_func_t func, drvinfo_t *drvinfo)
{
	struct giomap_init_data *gdata;
	static int	first_time = 1;

	FUNC_TRACE(giomap_config);
	gdata = drvinfo->per_device_data;

	switch(func) {

	case CFG_ADD:
		/* Add a new mapper instance to the UDI framework. We keep
		 * track of the number of GIO instances so that the gdata can
		 * be set to the region-local data
		 */

		/*
		 * If this is the first entry, we need to call giomap_start
		 * so that the correct BCB can be allocated. This has to happen
		 * at this late stage because the udi_glue magic doesn't allow
		 * us to call a secondary _load function [despite the comment
		 * at the top of this file :-(]
		 */
		if ( first_time ) {
			first_time = 0;
			giomap_start();
		}
		gdata = _OSDEP_MEM_ALLOC( sizeof( giomap_init_data_t ), 0, 0);
		if ( gdata == NULL ) {
			return ENODEV;
		}
	       
		gdata->instance = nGIOmappers;
		nGIOmappers++;

		gdata->rdata = drvinfo->per_device_rdata;
		drvinfo->per_device_data = gdata;
#if defined(DEBUG)
		DEBUGPRINT(( "giomap_config: added gdata(%p) {instance=%d,rdata=%p}\n",
			gdata, gdata->instance, gdata->rdata ));
#endif
		return 0;

	case CFG_REMOVE:
		/*
		 * Remove this mapper instance from UDI. The unbind() code
		 * should take care of this. All we need do is to update our
		 * count of the number of GIO mapper instances
		 */

		_OSDEP_ASSERT(nGIOmappers >= 1);
		nGIOmappers--;
#if defined(DEBUG)
		DEBUGPRINT(("giomap_config: removed gdata(%p) {rdata=%p}\n",
				gdata, gdata->rdata));
#endif

		_OSDEP_MEM_FREE( gdata );
		drvinfo->per_device_data = NULL;
		if ( nGIOmappers < 0 ) {
			nGIOmappers = 0;
		}
		return 0;
	case CFG_SUSPEND:
	case CFG_RESUME:
	default:
		return EOPNOTSUPP;
	}
}

#define	MY_MIN(a, b)	((a) < (b) ? (a) : (b))



/*
 *------------------------------------------------------------------------
 * Mapper Interface routines [called by giomap.c]
 *------------------------------------------------------------------------
 */
STATIC void giomap_OS_init( struct giomap_region_data * );
STATIC void giomap_OS_deinit( struct giomap_region_data * );
STATIC void giomap_OS_bind_done( udi_channel_event_cb_t *, udi_status_t );
#if 0
STATIC void giomap_OS_abort_ack( udi_gio_abort_cb_t *, udi_status_t );
#endif
STATIC void giomap_OS_io_done( udi_gio_xfer_cb_t *, udi_status_t );
STATIC void giomap_OS_Alloc_Resources( struct giomap_region_data * );
STATIC void giomap_OS_Free_Resources( struct giomap_region_data * );
STATIC void giomap_OS_channel_event( udi_channel_event_cb_t * );
STATIC void giomap_OS_unbind_done( udi_gio_bind_cb_t * );
STATIC void giomap_OS_event( udi_gio_event_cb_t * );

/*
 * Code Section
 */

#include "giocommon/giomap.c"		/* Common mapper code */

#if DEBUG
STATIC void dump_perdevdata(struct giomap_init_data *per_device_data)
{
	giomap_init_data_t	*gdata;
	giomap_region_data_t	*rdata;

	printk("per_device_data = giomap_init_data = %p\n", per_device_data);
	gdata = (giomap_init_data_t *)per_device_data;
	if (gdata) {
		rdata = (giomap_region_data_t *)gdata->rdata;
		if (rdata)
			printk("rdata = %p\n", rdata);
		else
			printk("rdata was NULL\n");
	}
	else {
		printk("gdata was NULL\n");
	}
}
#endif

#if UNIXWARE
/*
 * devinfo entry point -- FIXME: We need to support the underlying device's
 * constraint properties as and when they become available.
 * Is there a race-condition between the UW call to this devinfo routine and
 * the meta device bind which will instantiate the device-specific constraints ?
 */
STATIC int
giomap_devinfo( void *idatap, channel_t chan, di_parm_t parm, void *valp )
{
	bcb_t	**bcbp;

	FUNC_TRACE(giomap_devinfo);

	switch( parm ) {
	case DI_SIZE:	/* Size of device -- obtained from bind seq */
	{
		giomap_init_data_t *gdata = 
			(giomap_init_data_t *) idatap;
		giomap_region_data_t *rdata = 
			(giomap_region_data_t *) gdata->rdata;
		off64_t	dev_size = ( off64_t )rdata->dev_size_hi;
		daddr_t blkno;
		ushort_t blkoff;

		/* Compute full 64bit byte size of device */
		dev_size <<= 32;
		dev_size += rdata->dev_size_lo;

		/* Convert to block number and offset */
		blkno = dev_size >> GIOMAP_SEC_SHFT;
		blkoff = ( ushort_t ) (dev_size & ((1 << GIOMAP_SEC_SHFT)-1));

		((devsize_t *)valp)->blkoff = blkoff;
		((devsize_t *)valp)->blkno = blkno;
		return 0;
	}
	case DI_RBCBP:
	case DI_WBCBP:	/* Breakup-control-block settings */
		/* FIXME: Return the device specific bcb which is generated
		 *	  in response to a udi_constraints_propogate
		 */
		if (giomap_bcb) {
			bcbp = (bcb_t **)valp;
			*bcbp = giomap_bcb;
			return 0;
		} else {
			return EOPNOTSUPP;
		}
	default:
		return EOPNOTSUPP;
	}
}
#endif /*UNIXWARE*/
/*
 *------------------------------------------------------------------------
 * Local OS-specific mapper routines [not called from giomap.c]
 *------------------------------------------------------------------------
 */
STATIC void giomap_req_enqueue( giomap_queue_t * );
STATIC void giomap_req_release( giomap_queue_t * );
STATIC udi_buf_write_call_t giomap_req_buf_cbfn;
#if 0
STATIC giomap_queue_t *giomap_abort( giomap_region_data_t *, void * );
STATIC void giomap_abort_release( giomap_queue_t * );
#endif

STATIC giomap_queue_t *giomap_getQ( giomap_region_data_t *, void *, 
				    giomap_elem_t );
STATIC void giomap_send_req( giomap_queue_t * );
STATIC giomap_queue_t *giomap_buf_enqueue( giomap_region_data_t *,
					giomap_buf_t * );
STATIC udi_mem_alloc_call_t _giomap_kernbuf_cbfn;
#if 0
STATIC void _giomap_got_abortcb( udi_cb_t *, udi_cb_t * );
STATIC void _giomap_got_abortmem( udi_cb_t *, void * );
#endif
STATIC udi_mem_alloc_call_t _giomap_got_alloc_cb;
STATIC udi_cb_alloc_call_t _giomap_got_req_RW_cb;
STATIC udi_cb_alloc_call_t _giomap_got_req_DIAG_cb;
#if 0
STATIC udi_cb_alloc_call_t _giomap_got_req_USER_cb;
#endif
STATIC udi_mem_alloc_call_t _giomap_got_reqmem;
STATIC void _giomap_getnext_rsrc( giomap_region_data_t * );

STATIC void giomap_calc_offsets( udi_ubit32_t , udi_ubit32_t , udi_ubit32_t *,
				 udi_ubit32_t * );
STATIC giomap_mod_t *giomap_find_modname( const char * );
STATIC udi_ubit32_t _giomap_adjust_amount( giomap_region_data_t *, 
					   giomap_queue_t * );
STATIC udi_timer_expired_call_t _giomap_fake_ack;

#if UNIXWARE
STATIC int
giomap_new_instance( gio_map_req_t *req )
{
	FUNC_TRACE(giomap_new_instance);

#if 0 /* UNIXWARE also is = 0 */
	int rval;
	giomap_region_data_t	*rdata;

	rval = _udi_mapper_load(MAPPER_NAME, init_module);
	if ( rval ) {
#if defined(DEBUG)
		DEBUGPRINT(("giomap_new_instance: _udi_mapper_load FAILED\n"));
#endif	/* DEBUG */
		rval = ENODEV;	/* Fail */
	} else {
		req->arg = _udi_MA_local_data(MAPPER_NAME, nGIOmappers);
		if ( req->arg == NULL ) {
#if defined(DEBUG)
			DEBUGPRINT((
				"giomap_new_instance: _udi_MA_local_data(%s, %d) FAILED\n",
			MAPPER_NAME, nGIOmappers));
#endif	/* DEBUG */
			(void)_udi_mapper_unload(MAPPER_NAME);
			rval = ENODEV;
		} else {
			nGIOmappers++;
			rval = 0;
			rdata = (giomap_region_data_t *)req->arg;
			/* TODO: Save incoming resmgr key to allow us to back
			 *	 link the connection for received data
			 */
		}
	}

	return rval;
#else
	return 0;
#endif

}

STATIC int
giomap_del_instance( gio_map_req_t *req )
{
FUNC_TRACE(giomap_del_instance);
	/* TODO: perform a _udi_MA_unbind on the GIO mapper */
	return 0;
}

/*
 * giomap_drvctl:
 * -------------
 * Handle inter-mapper calls [from NET, SCSI etc.] The requested operations
 * supported are:
 *		GIO_ADD_INSTANCE
 *			Add a new GIO mapper instance to the UDI system [like
 *			CFG_ADD does]
 *		GIO_DEL_INSTANCE
 *			Remove the given GIO instance from the system [similar
 *			to CFG_REMOVE]
 *
 * Return Values:
 *	0		success
 *	EINVAL		failure
 */
STATIC int
giomap_drvctl( void *idatap, channel_t channel, int cmd, void *arg )
{
	gio_map_req_t	*req = (gio_map_req_t *)arg;
	int		rval;

	FUNC_TRACE(giomap_drvctl);

	switch ( cmd ) {
	case GIO_ADD_INSTANCE:
		/* Add new instance of GIO mapper to UDI */
		rval = giomap_new_instance( req );
		return rval;
	case GIO_DEL_INSTANCE:
		rval = giomap_del_instance( req );
		return rval;
	default:
		return EINVAL;
	}
}
#endif

/*
 * ---------------------------------------------------------------------------
 * User interface
 * ---------------------------------------------------------------------------
 */

STATIC int 
giomap_open_uw(void *idatap, channel_t *channelp)
{
	giomap_init_data_t	*gdata = idatap;
	giomap_region_data_t	*rdata = (giomap_region_data_t *)gdata->rdata;
	channel_t		channel = *channelp;

	rdata = (giomap_region_data_t *)gdata->rdata;
	DEBUGPRINT(("giomap_open_uw: instance %08X's rdata = %p\n",
							gdata->instance, rdata));

	/* Allow pass-through open to succeed on control channel -- called in
	 * response to a drv_open() from another mapper
	 */
	
	if ( channel == GIO_PASSTHRU ) {
		return 0;
	}

	if ( rdata == NULL ) {
		/* Driver hasn't been bound by MA */
		_OSDEP_PRINTF("giomap_open_uw: driver hasn't been bound.\n");
		return ENXIO;
	}
	
	/*
	 * Stash the channel into the region-local data so we can get the
	 * correct gdata[] index from UDI-based operations
	 */
	rdata->channel = ( udi_ubit32_t )channel;
	return 0;
}

#ifdef UNIXWARE
STATIC int 
giomap_close(void *idatap, channel_t channel, queue_t *q)
{
	return 0;
}
#endif

STATIC int giomap_ioctl (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	giomap_init_data_t	*gdata = (giomap_init_data_t *)
					giomap_get_per_device_data(filp);
	giomap_region_data_t	*rdata = (giomap_region_data_t *)gdata->rdata;
	giomap_uio_t		uio_req, *uio_p;
	udi_gio_xfer_cb_t	*ioc_cb;
	giomap_queue_t		*qelem;
	udi_status_t		status;
	udi_size_t		xfer_len;
	channel_t		channel;

	FUNC_TRACE(giomap_ioctl);

#if 0 /* UNIXWARE is also = 0 */
	if ( gdata->nelem <= channel ) {
		return ENXIO;
	} else {
		rdata = (giomap_region_data_t *)gdata->list[channel];
	}
#endif

	if ( rdata == NULL ) {
		return ENXIO;
	}
	
	channel = giomap_get_channel(filp);
	/* Paranoia check to make sure we're talking to the right device */
	GIOMAP_ASSERT( rdata->channel == channel );

	switch ( cmd ) {
	case UDI_GIO_DATA_XFER:
		/*
		 * Issue udi_gio_data_xfer_req();
		 * wait for udi_gio_data_xfer_ack/nak
		 */
		if ( copyin( (void*)arg, (void*)&uio_req, sizeof( giomap_uio_t )) ) {
			return EFAULT;
		}
		uio_p = &uio_req;

		uio_p->u_resid = uio_p->u_count;
		uio_p->u_count = 0;

		/*
		 * Ensure that tr_param size does not exceed what we've
		 * previously allocated. If it does, we fail the request.
		 * TODO: Need to (maybe) handle dynamic reallocation of
		 *	 xfer_cb size
		 */
		if ( uio_p->tr_param_len > UDI_GIO_MAX_PARAMS_SIZE ) {
			uio_p->u_error = UDI_STAT_NOT_SUPPORTED;
			(void)copyout( (void*)uio_p, (void*)arg, sizeof( giomap_uio_t ) );
			return EINVAL;
		}
		
		/*
		 * Validate the operation request for asynchronous i/o
		 * We can only run asynchronously if there is no associated
		 * data transfer to the user application.
		 * Async write()s are supported, as are operations which have
		 * no associated data transfer.
		 * Async read()s will fail with UDI_STAT_NOT_SUPPORTED
		 */
		
		if ( (uio_p->u_async) && (uio_p->u_op & UDI_GIO_DIR_READ) ) {
			uio_p->u_error = UDI_STAT_NOT_SUPPORTED;
			(void)copyout( (void*)uio_p, (void*)arg, sizeof( giomap_uio_t ) );
			return EINVAL;
		}

		/* Get a queue element for the request -- may block */
		qelem = giomap_getQ( rdata, uio_p, Ioctl );

		while ( (qelem->status == UDI_OK) &&
			(qelem->uio_p->u_resid > 0 ) ) {

			/* Process request  -- may block */
			giomap_req_enqueue( qelem );

			/*
			 * Bail out if we encountered an error
			 */
			if ( qelem->status != UDI_OK ) {
				break;
			}

			/*
			 * Check to see if we can return an asynchronous
			 * request. This will work if the amount of data will
			 * fit into the qelem's kernel buffer. Otherwise we
			 * have to issue multiple requests and can only
			 * return the async handle when we have sent the last
			 * block to the driver
			 */
			if ( (uio_p->u_async) && (qelem->single_xfer) ) {
				qelem->uio_p->u_handle = qelem;

				/* copy updated request back to user */
				(void)copyout((void*)qelem->uio_p, (void*)arg, 
					    sizeof( giomap_uio_t ));
				return 0;
			}

			/* Copy data out to user space if OP_DIR_READ set */

			ioc_cb = UDI_MCB( qelem->cbp, udi_gio_xfer_cb_t );

			if ( uio_p->u_op & UDI_GIO_DIR_READ ) {
				xfer_len = ioc_cb->data_buf->buf_size;
				if ( xfer_len > 0 ) {
					udi_buf_read(ioc_cb->data_buf, 0, 
					     xfer_len,
					     qelem->kernbuf );
				
					(void)copyout( (void*)qelem->kernbuf,
						(void*)qelem->uio_p->u_addr,
					    	xfer_len );
				}
				
				qelem->uio_p->u_resid -= xfer_len;
				qelem->uio_p->u_count += xfer_len;
				/* Handle device data exhaustion */
				if ( xfer_len == 0 ) {
					break;
				}
			} else if ( uio_p->u_op & UDI_GIO_DIR_WRITE ) {
				xfer_len = qelem->xfer_len;
				qelem->uio_p->u_resid -= xfer_len;
				qelem->uio_p->u_count += xfer_len;
			}
		}

		/*
		 * Completed transfer
		 */
		qelem->uio_p->u_error = qelem->status;

		status = qelem->status;

		(void)copyout( (void*)qelem->uio_p, (void*)arg, sizeof( giomap_uio_t ) );
		giomap_req_release( qelem );

		return (status ? EIO : 0);


#ifdef LATER
	case UDI_GIO_INQUIRY:
		/*
		 * Issue udi_gio_data_xfer_req()
		 * wait for udi_gio_data_xfer_ack/nak
		 */
		_OSDEP_EVENT_WAIT( &rdata->giomap_data_xfer );
		return rdata->last_xfer_status;
#endif
	default:
		return EINVAL;	/* Unsupported command */
	}
}

/*
 * biostart() interface:
 *	Issue a READ or WRITE request.
 *	Writes are expected to complete in one shot, so any outstanding data
 *	will be passed back to the application.
 *
 * NOTE: This routine is called with the user-supplied data already mapped and
 *	 locked into the system address space. This allows us to use the
 *	 supplied buffer addresses as source/destination for the udi_buf_xxxx
 *	 routines without having to perform a mapping operation first.
 */
/* Return values are 0, no error; else, error. */
STATIC int
giomap_biostart(void *idatap, channel_t channel, buf_t *buf_p, ssize_t *amount)
{
	giomap_init_data_t	*gdata = (giomap_init_data_t *)idatap;
	giomap_region_data_t	*rdata = (giomap_region_data_t *)gdata->rdata;
	udi_gio_xfer_cb_t	*xfer_cb;
	int			retval;
	giomap_queue_t		*qelem;
	udi_size_t		xfer_len;

	*amount = 0;
#if 0 /* not used on Unixware */
	if ( gdata->nelem <= channel ) {
		return ENXIO;
	} else {
		rdata = gdata->list[channel];
	}
#endif

	if ( rdata == NULL ) {
		return ENXIO;
	}

	/* Paranoia check to ensure we're talking to the correct device */
	GIOMAP_ASSERT( rdata->channel == channel );

	buf_p->b_resid = buf_p->b_bcount;
	if ( rdata == NULL ) {
		return ENXIO;
	}

	/* Get handle for request */
	qelem = giomap_buf_enqueue(rdata, buf_p);

	/* Wait on the request to complete */
	_OSDEP_EVENT_WAIT( &qelem->event );
	
	/*
	 * Determine how much data (if any) has been transferred
	 */
	xfer_cb = UDI_MCB( qelem->cbp, udi_gio_xfer_cb_t );

	if ( buf_p->b_flags & GIOMAP_B_READ ) {
		/*
		 * Copy new data to application.
		 */
		xfer_len = xfer_cb->data_buf->buf_size;
		udi_buf_read(xfer_cb->data_buf, 0, xfer_len,
			     buf_p->b_un.b_addr);
		buf_p->b_resid -= xfer_len ;
	} else {
		xfer_len = qelem->xfer_len;
		buf_p->b_resid -= xfer_len;
	}

	*amount = (int)xfer_len;

	/*
	 * Update the residual count and error flags so that the calling
	 * application will get the correct return from the pseudo read/write
	 * call
	 */
	retval = qelem->status;

	/*
	 * Make queue element available for subsequent use
	 */
	giomap_req_release( qelem );

	return retval;
}


/*
 * Support Routines
 */

/*
 * giomap_getQ:
 * -----------
 *	Return a queue element suitable for the given <type> of operation.
 *	The originating request <req_p> will be copied into the per-element
 *	data structure to allow for subsequent asynchronous UDI context
 *	processing to occur.
 *	This routine will block until a queue element is made available
 *
 * Return Value:
 *	Request element which is suitable for use in a udi_gio_xfer_req op.
 */
STATIC giomap_queue_t *
giomap_getQ(
	giomap_region_data_t	*rdata,
	void			*req_p,
	giomap_elem_t		type )
{
	giomap_queue_t	*qelem;
	udi_gio_op_t	opcode;

	FUNC_TRACE(giomap_getQ);

	DEBUGPRINT(("getQ working with rdata(%p) %s\n",
        	rdata, ((_udi_channel_t*)UDI_GCB(rdata->my_bind_cb)->channel)->chan_region->reg_driver->drv_name));

	_OSDEP_MUTEX_LOCK( &rdata->xfer_lock );
	while ( UDI_QUEUE_EMPTY( &rdata->xfer_q.Q ) ) {
		_OSDEP_MUTEX_UNLOCK(&rdata->xfer_lock);
		_OSDEP_EVENT_WAIT( &rdata->xfer_q_event );
		_OSDEP_MUTEX_LOCK( &rdata->xfer_lock );
	}
#if DEBUG
	DEBUGPRINT(("xfer_q.numelem = %d\n", rdata->xfer_q.numelem));
	if (rdata) {
		DEBUGPRINT(("Q->next = %p, ", rdata->xfer_q.Q.next));
		if (rdata->xfer_q.Q.next) {
			DEBUGPRINT(("Q->next->prev=%p, ",
				rdata->xfer_q.Q.next->prev));
			DEBUGPRINT(("Q->next->next=%p\n",
				rdata->xfer_q.Q.next->next));
		}
		else DEBUGPRINT(("Q.next was null\n"));
	}
	else DEBUGPRINT(("rdata was null\n"));
#endif
	if (rdata->xfer_q.Q.next != NULL) {
		qelem = (giomap_queue_t *)UDI_DEQUEUE_HEAD( &rdata->xfer_q.Q );
	}
	else {
		qelem = NULL;
	}
	rdata->xfer_q.numelem--;
	_OSDEP_MUTEX_UNLOCK( &rdata->xfer_lock );
	/* Assert outside of mutexes so the machine can recover from the Oops. */
	_OSDEP_ASSERT(qelem != NULL);
	/*
	 * Copy originating request into per-element data structure
	 */
	qelem->type = type;
	qelem->status = UDI_OK;

	switch ( type ) {
	case Biostart:
		/* Kernel-based request */
		udi_memcpy( qelem->buf_p, req_p, sizeof( giomap_buf_t ) );
		break;
	case Ioctl:
		/* User-based request */
		udi_memcpy( qelem->uio_p, req_p, sizeof( giomap_uio_t ) );
		qelem->prev_count = 0;
		/*
		 * Set up the correct CB to use for the request. For ordinary
		 * GIO_OP_READ/GIO_OP_WRITE we use the <rw_cb>;
		 * For GIO_OP_DIAG_RUN_TEST and other ops we use <diag_cb> 
		 * which contains up to UDI_GIO_MAX_PARAMS_SIZE bytes
		 */
		opcode = qelem->uio_p->u_op;
		if ( opcode == UDI_GIO_OP_READ || opcode == UDI_GIO_OP_WRITE ) {
			qelem->cbp = UDI_GCB(qelem->rw_cb);
			qelem->cb_type = Xfer;
		} else {
			qelem->cbp = UDI_GCB(qelem->diag_cb);
			qelem->cb_type = Diag;
		} 
		break;
	}

	return qelem;

}

/*
 * giomap_buf_enqueue:
 * ------------------
 *	Enqueue a udi_gio_xfer_req request based on the passed in user-supplied
 *	parameters. The data has been previously mapped into system space which
 *	allows us to use the addresses as source/destination for the udi_buf_
 *	operations.
 *	We take a queue element from our list of available elements [xfer_q]
 *	and potentially block if the list is empty. This is allowable as we
 *	are running in user context (not UDI context).
 *	Once we have a request block we save the user-supplied data and start
 *	performing UDI channel operations. This puts us firmly into the UDI
 *	context which means we can no longer use _OSDEP_EVENT_WAIT or
 *	_OSDEP_MUTEX_LOCK from this execution thread.
 *
 * Return value:
 *	request element which is being used for the udi_gio_xfer_req operation.
 *	The caller should wait for the per-element event to be signalled before
 *	continuing with the user application thread. This signalling is done by
 *	giomap_OS_io_done()
 *
 *          called on completion of a udi_gio_xfer_req for both
 *          successful and unsuccessful completion cases
 */
STATIC giomap_queue_t *
giomap_buf_enqueue(
	giomap_region_data_t	*rdata,		/* Region-local data */
	giomap_buf_t		*buf_p )	/* User request */
{
	udi_gio_xfer_cb_t *xfer_cb;
	udi_boolean_t	   big_enough;
	giomap_queue_t	  *qelem;
	udi_ubit32_t	   amount;

	FUNC_TRACE(giomap_buf_enqueue);

	qelem = giomap_getQ( rdata, buf_p, Biostart );

	qelem->cbp = UDI_GCB(qelem->rw_cb);
	qelem->cb_type = Xfer;

	xfer_cb = qelem->rw_cb;

	/*
	 * We've now got a queue element and its associated CB.
	 * Update it to reference the user-supplied request and start the
	 * allocation chain off.
	 *
	 * NOTE: once we start UDI_BUF_ALLOC'ing we have no user state
	 *	 available. This requires us to stash the queue element address
	 *	 in the xfer_cb's initiator context.
	 */

	if ( qelem->buf_p->b_flags & GIOMAP_B_READ ) {
		xfer_cb->op = UDI_GIO_OP_READ;
	} else {
		xfer_cb->op = UDI_GIO_OP_WRITE;
	}

	/*
	 * Adjust the amount to avoid accessing beyond the end of the device
	 * if it has a non-zero di_size
	 */
	amount = qelem->buf_p->b_bcount;

	/*
	 * Determine size of buffer associated with <xfer_cb>
	 * If its large enough to hold the data (buf_p->b_bcount) we don't
	 * need to allocate a new one.
	 */
	if ( xfer_cb->data_buf != NULL ) {
		big_enough = xfer_cb->data_buf->buf_size >= amount;
	} else {
		big_enough = 0;
	}

	qelem->xfer_len = amount;
	xfer_cb->gcb.initiator_context = qelem;	/* Reverse link */

	/*
	 * Obtain a UDI buffer to hold the outgoing / incoming data. The
	 * allocation will instantiate the data to the passed in buffer
	 * contents.
	 */
	
	if ( (xfer_cb->op & (UDI_GIO_DIR_READ|UDI_GIO_DIR_WRITE)) &&
	     (amount > 0) ) {
		if ( xfer_cb->op & UDI_GIO_DIR_READ ) {
			/* Read into buffer */
			if ( !big_enough ) {
				udi_buf_free( xfer_cb->data_buf );
				/* Allocate new buffer */
				UDI_BUF_ALLOC( giomap_req_buf_cbfn, qelem->cbp, 
						NULL, amount, 
						rdata->buf_path);
			} else {
				/* Re-use the existing buffer */
				/*
				 * We have to delete any extraneous bytes from
				 * the buffer so that the buf_size is correctly
				 * updated. As we cannot delete 0 bytes (ahem)
				 * we need to special case this
				 */
				if ( xfer_cb->data_buf->buf_size > amount ) {
				    UDI_BUF_DELETE( giomap_req_buf_cbfn, 
				    	qelem->cbp,
					xfer_cb->data_buf->buf_size - amount,
					xfer_cb->data_buf, 0 );
				} else {
				    giomap_req_buf_cbfn( qelem->cbp,
				    	xfer_cb->data_buf );
				}
			}
	    	} else {
			xfer_cb->op = UDI_GIO_OP_WRITE;
			/* Write from buffer */
			if ( !big_enough ) {
				udi_buf_free( xfer_cb->data_buf );
				/* Allocate and fill new buffer */
				UDI_BUF_ALLOC( giomap_req_buf_cbfn, qelem->cbp, 
						buf_p->b_un.b_addr, buf_p->b_bcount, 
						rdata->buf_path );
			} else {
				/* Re-use the existing buffer */
				udi_buf_write( giomap_req_buf_cbfn, qelem->cbp,
						buf_p->b_un.b_addr, buf_p->b_bcount,
						xfer_cb->data_buf, 0, 
						buf_p->b_bcount,
						UDI_NULL_BUF_PATH );
			}
	    	}
	} else {
		udi_buf_free( xfer_cb->data_buf );
		xfer_cb->data_buf = NULL;
		/* Call parent driver directly... */
		UDI_GCB(xfer_cb)->channel = UDI_GCB(rdata->my_bind_cb)->channel;
		udi_gio_xfer_req( xfer_cb );
	}
	return qelem;
}

/*
 * giomap_req_enqueue:
 * ------------------
 *	Enqueue a udi_gio_xfer_req request based on the passed in user-supplied
 *	parameters. This will be an ioctl()-based request and will require
 *	mapping the data buffers into kernel space [maximum of giomap_bufsize
 *	per transfer]
 *	This routine may be called multiple times to satisfy one user request.
 *	If the u_count field is non-zero, it means we're on a subsequent
 *	iteration.
 *
 *	TODO: handle arbitrary udi_layout_t specifications in a proper manner
 */
STATIC void
giomap_req_enqueue(
	giomap_queue_t	*qelem )		/* Request element */
{
	udi_gio_xfer_cb_t *xfer_cb = UDI_MCB( qelem->cbp, udi_gio_xfer_cb_t );
	udi_ubit32_t	   amount = qelem->uio_p->u_resid;
	udi_gio_rw_params_t *rwparams;
	udi_ubit32_t	   user_addr;
	giomap_region_data_t	*rdata = UDI_GCB(xfer_cb)->context;

	FUNC_TRACE(giomap_req_enqueue);

	GIOMAP_ASSERT( xfer_cb->tr_params != NULL );

	rwparams = xfer_cb->tr_params;
	xfer_cb->op = qelem->uio_p->u_op;
	xfer_cb->gcb.initiator_context = qelem;

	/*
	 * Validate the command bitfields. It is not permissible to have both
	 * UDI_GIO_DIR_READ and UDI_GIO_DIR_WRITE set in the same <op> field
	 */
	if ( (xfer_cb->op ^ 
		( udi_gio_op_t )(UDI_GIO_DIR_READ|UDI_GIO_DIR_WRITE)) ==
		( udi_gio_op_t )~(UDI_GIO_DIR_READ|UDI_GIO_DIR_WRITE)) {
		qelem->status = UDI_STAT_NOT_UNDERSTOOD;
		return;
	}

	/*
	 * Check to see if this request will fit into one transaction. If so,
	 * we don't have to special-case the data buffer mapping
	 */
	qelem->single_xfer = amount <= rdata->giomap_bufsize;


	/*
	 * Copy next block of data into kernel space for UDI_GIO_OP_WRITE
	 * Any other command (with the DIR_WRITE bit set) is unsupported as we
	 * do not know what the tr_params contents are. This makes it impossible
	 * to adjust a device offset to split the request into smaller chunks.
	 */
	if ( !qelem->single_xfer && (xfer_cb->op & UDI_GIO_DIR_WRITE) &&
		(xfer_cb->op != UDI_GIO_OP_WRITE) ) {
		qelem->status = UDI_STAT_NOT_SUPPORTED;
		return;
	}

	/*
	 * Allocate ourselves a buffer of giomap_bufsize bytes to use as a
	 * staging area between user and kernel space. We do this the first
	 * time through if there is data to transfer
	 */
	
	if ( (qelem->uio_p->u_count == 0) && 
	     (xfer_cb->op & (UDI_GIO_DIR_WRITE|UDI_GIO_DIR_READ) ) ) {

		if ( qelem->kernbuf == NULL ) {
			/*
		 	 * Allocate a buffer, the callback will handle the 
			 * initial copy in the GIO_DIR_READ case
		 	 */
			udi_mem_alloc( _giomap_kernbuf_cbfn, qelem->cbp,
					rdata->giomap_bufsize, UDI_MEM_NOZERO );
		} else {
			/*
			 * Re-use the pre-allocated kernbuf
			 */
			_giomap_kernbuf_cbfn( qelem->cbp, qelem->kernbuf );
		}
			
		if ( !qelem->uio_p->u_async || !qelem->single_xfer ) {
			_OSDEP_EVENT_WAIT( &qelem->event );
		} 
		return;
	}

	/*
	 * Now we have to copy in user data for the next [amount] bytes. If
	 * we still have more data than will fit into qelem->kernbuf, we go
	 * through this code again [from ioctl()]
	 * We need to update the tr_params field to make sure we transfer the
	 * data to/from the correct part of the device.
	 */
	if (xfer_cb->op == UDI_GIO_OP_WRITE || xfer_cb->op == UDI_GIO_OP_READ) {
		/* Our offset is giomap_bufsize bytes further on */
#if 1
		printk("rwparams->offset_lo=%d >= GIOMAP_MAX_OFFSET=%d\n",
			rwparams->offset_lo, GIOMAP_MAX_OFFSET);
#endif
		if ( rwparams->offset_lo >= GIOMAP_MAX_OFFSET ) {
			rwparams->offset_lo += qelem->uio_p->u_count -
					       qelem->prev_count;
			rwparams->offset_hi++;
		} else {
			rwparams->offset_lo += qelem->uio_p->u_count -
					       qelem->prev_count;
		}
#if 1
		printk("rwparams->offset_lo=%d  rwparams->offset_hi=%d\n",
			rwparams->offset_lo, rwparams->offset_hi);
#endif
	}
	/*
	 * Update user source/destination address 
	 */
	user_addr = (udi_ubit32_t )qelem->uio_p->u_addr;
	user_addr += qelem->uio_p->u_count - qelem->prev_count;
	qelem->uio_p->u_addr = (void *)user_addr;

	qelem->prev_count = qelem->uio_p->u_count;

	/*
	 * Update amount to reflect maximum transfer we're going to perform
	 */
	if ( !qelem->single_xfer )
		amount = rdata->giomap_bufsize;

	/*
	 * Copy data into kernel buffer -- does not use UDI buffer scheme as
	 * we're emulating copyin(). The READ data will be copied into the
	 * kernbuf and then copied out to user-space by the ioctl() routine.
	 */
	if ( xfer_cb->op & UDI_GIO_DIR_WRITE ) {
		if ( copyin( (void*)qelem->uio_p->u_addr, (void*)qelem->kernbuf, amount )) {
			qelem->status = EFAULT;
			return;
		}
#if 0
		udi_memcpy( qelem->kernbuf, qelem->uio_p->u_addr, amount );
#endif
	}

	/* Now we can stage the request */

	giomap_send_req( qelem );

	if ( !qelem->uio_p->u_async || !qelem->single_xfer ) {
		_OSDEP_EVENT_WAIT( &qelem->event );
	}
	return;
}

/*
 * _giomap_kernbuf_cbfn:
 * --------------------
 *	Called on completion of kernel buffer allocation to hold data which is
 *	destined / sourced from user space. If we are initially writing to the
 *	device we copy the first giomap_bufsize bytes into the newly allocated
 *	buffer. This emulates copyin()
 */
STATIC void
_giomap_kernbuf_cbfn(
	udi_cb_t	*gcb,
	void		*new_mem )
{
	udi_gio_xfer_cb_t	*xfer_cb = UDI_MCB( gcb, udi_gio_xfer_cb_t );
	giomap_queue_t		*qelem = xfer_cb->gcb.initiator_context;
	udi_ubit32_t		amount;
	giomap_region_data_t	*rdata = gcb->context;

	FUNC_TRACE(_giomap_kernbuf_cbfn);

	amount = qelem->uio_p->u_resid;
	if ( amount > rdata->giomap_bufsize )
		amount = rdata->giomap_bufsize;

	qelem->kernbuf = new_mem;
	if ( qelem->uio_p->u_op & UDI_GIO_DIR_WRITE ) {
		(void)copyin((void*)qelem->uio_p->u_addr, (void*)qelem->kernbuf, amount);
#if 0
		udi_memcpy( qelem->kernbuf, qelem->uio_p->u_addr, amount );
#endif
	}

	/*
	 * Copy the user-specified tr_params over our xfer_cb ones.
	 * The size of the params must be less than the maximum size we
	 * preallocated in udi_gio_xfer_cb_init()
	 */
	if ( qelem->uio_p->tr_param_len ) {
		(void)copyin((void*)qelem->uio_p->tr_params, (void*)xfer_cb->tr_params,
				qelem->uio_p->tr_param_len);
#if 0
		udi_memcpy( xfer_cb->tr_params, qelem->uio_p->tr_params,
			    qelem->uio_p->tr_param_len );
#endif
	}

	/* Zero the rwparams if this device does not have a size */
	if ( (xfer_cb->op == UDI_GIO_OP_WRITE || xfer_cb->op == UDI_GIO_OP_READ)
	    && ((rdata->dev_size_lo == 0) && (rdata->dev_size_hi == 0))) {
	    	udi_gio_rw_params_t	*rwparams = xfer_cb->tr_params;
#if 1
		printk("Zeroing device rwparams because device has no size.\n");
#endif
		rwparams->offset_lo = rwparams->offset_hi = 0;
	}
	    	

	/* Issue request to driver */
	giomap_send_req( qelem );
}

/*
 * giomap_send_req:
 * ---------------
 *	Issue a user mapped request to the underlying driver. Write requests
 *	will have their mapped data in qelem->kernbuf. Read requests will
 *	be mapped out to user space by the originating ioctl() call.
 *
 *	The size of the transfer will be a maximum of giomap_bufsize 
 *
 *	NOTE: We must remove the element from its queue to avoid adding the
 *	      same element multiple times to the xfer_inuse_q.  This only
 *	      happens when we split a transfer request into multiple chunks for
 *	      the Ioctl case.
 */
STATIC void
giomap_send_req(
	giomap_queue_t	*qelem )
{
	udi_gio_xfer_cb_t	*xfer_cb;
	udi_boolean_t		big_enough;
	udi_ubit32_t		amount;
	void			*user_addr = qelem->kernbuf;
	giomap_region_data_t	*rdata;

	FUNC_TRACE(giomap_send_req);

	xfer_cb = UDI_MCB( qelem->cbp, udi_gio_xfer_cb_t );
	rdata   = UDI_GCB(xfer_cb)->context;
	
	/*
	 * Adjust amount so that we don't overrun the device limits for READ/
	 * WRITE operations. Any other operation is passed directly to the
	 * provider
	 */
	if ( (rdata->dev_size_lo || rdata->dev_size_hi) &&
	      ((xfer_cb->op == UDI_GIO_OP_WRITE) || 
	       (xfer_cb->op == UDI_GIO_OP_READ)) ) {
		amount = _giomap_adjust_amount( rdata, qelem );
	} else {
		amount = qelem->uio_p->u_resid;
	}

	if ( amount > rdata->giomap_bufsize )
		amount = rdata->giomap_bufsize;

	/*
	 * Determine size of buffer associated with <xfer_cb>
	 * If its large enough to hold the data (buf_p->b_bcount) we don't
	 * need to allocate a new one.
	 */
	if ( xfer_cb->data_buf != NULL ) {
		big_enough = xfer_cb->data_buf->buf_size >= amount;
	} else {
		big_enough = 0;
	}

	qelem->xfer_len = amount;

	/*
	 * Remove queue element from any active queue _before_ we start any
	 * asynchronous processing
	 */
	UDI_QUEUE_REMOVE( &qelem->Q );

	if ( (xfer_cb->op & (UDI_GIO_DIR_READ|UDI_GIO_DIR_WRITE)) ) {
		/*
		 * Handle 0 length data transfer ops specially. We don't pass
		 * these down to the driver as they won't (or shouldn't) do
		 * anything. Instead we schedule a callback to happen as soon
		 * as possible (min_timer_res) nanoseconds so that we mimic
		 * driver completion of the routine.
		 */
		if ( amount == 0 ) {
			udi_time_t	intvl;

			intvl.seconds = 0;
			intvl.nanoseconds = 
				rdata->init_context.limits.min_timer_res;

			UDI_GCB(xfer_cb)->channel = 
				UDI_GCB(rdata->my_bind_cb)->channel;
			udi_timer_start(_giomap_fake_ack, UDI_GCB(xfer_cb),
					intvl);
			return;
		}
		if ( xfer_cb->op & UDI_GIO_DIR_READ ) {
			/* Read into buffer */
			if ( !big_enough ) {
				udi_buf_free( xfer_cb->data_buf );
				/* Allocate new buffer */
				UDI_BUF_ALLOC( giomap_req_buf_cbfn, qelem->cbp, 
						NULL, amount, rdata->buf_path );
			} else {
				/* Re-use the existing buffer */
				/*
				 * We have to delete any extraneous bytes from
				 * the buffer so that the buf_size is correctly
				 * updated. As we cannot delete 0 bytes (ahem)
				 * we need to special case this
				 */
				if ( xfer_cb->data_buf->buf_size > amount ) {
				    UDI_BUF_DELETE( giomap_req_buf_cbfn, 
				    	qelem->cbp,
					xfer_cb->data_buf->buf_size - amount,
					xfer_cb->data_buf, 0 );
				} else {
				    giomap_req_buf_cbfn( qelem->cbp,
					xfer_cb->data_buf );
				}
			}
	    	} else {
			/* Write from buffer */
			if ( !big_enough ) {
				udi_buf_free( xfer_cb->data_buf );
				/* Allocate and fill new buffer */
				UDI_BUF_ALLOC( giomap_req_buf_cbfn, qelem->cbp, 
						user_addr, amount, 
						rdata->buf_path );
			} else {
				/* Shrink buffer size to <amount> */
				xfer_cb->data_buf->buf_size = amount;
				/* Re-use the existing buffer */
				udi_buf_write( giomap_req_buf_cbfn, qelem->cbp,
						user_addr, amount,
						xfer_cb->data_buf, 0, amount,
						UDI_NULL_BUF_PATH );
			}
	    	}
	} else {
		udi_buf_free( xfer_cb->data_buf );
		xfer_cb->data_buf = NULL;
		/* Call parent driver directly... */
		UDI_GCB(xfer_cb)->channel = UDI_GCB(rdata->my_bind_cb)->channel;
		udi_gio_xfer_req( xfer_cb );
	}
}

/*
 * giomap_req_buf_cbfn:
 * -------------------
 * 	Callback function for buffer allocation. We have a reference to the
 * 	(unattached) queue element which corresponds to the user originated
 * 	biostart() request.
 * 	We simply place this request on the xfer_inuse_q and pass it down to the
 * 	underlying driver.
 */
STATIC void
giomap_req_buf_cbfn(
	udi_cb_t	*gcb,
	udi_buf_t	*new_buf )
{
	giomap_region_data_t	*rdata = gcb->context;
	udi_gio_xfer_cb_t	*xfer_cb = UDI_MCB( gcb, udi_gio_xfer_cb_t );
	udi_gio_rw_params_t	*rwparams;
	giomap_queue_t		*qelem = xfer_cb->gcb.initiator_context;

	FUNC_TRACE(giomap_req_buf_cbfn);
	/*
	 * Fill in the remaining fields of the xfer_cb
	 */
	
	xfer_cb->data_buf = new_buf;

	switch( qelem->type ) {
	case Biostart:
		rwparams = xfer_cb->tr_params;
		/* Convert block offset to byte offset */
		giomap_calc_offsets( qelem->buf_p->b_blkno, 
				     qelem->buf_p->b_blkoff,
				     &(rwparams->offset_hi),
				     &(rwparams->offset_lo) );
		GIOMAP_ASSERT( rwparams == xfer_cb->tr_params );
		break;
	case Ioctl:
		/*
		 * The rw_params field has been filled in by the enqueue
		 * function. The user supplies the tr_params for the request
		 * and we only have to update it if the request needs to be
		 * split into multiple chunks
		 */
		break;
	default:
		GIOMAP_ASSERT( (qelem->type == Biostart) || 
				(qelem->type == Ioctl) );
		break;
	}

	/*
	 * Put request on head of inuse elements. This provides a mechanism
	 * for determining what requests need to be aborted
	 */
	UDI_ENQUEUE_HEAD( &rdata->xfer_inuse_q.Q, (udi_queue_t *)qelem );
	rdata->xfer_inuse_q.numelem++;

	UDI_GCB(xfer_cb)->channel = UDI_GCB(rdata->my_bind_cb)->channel;
	udi_gio_xfer_req( xfer_cb );
}

/*
 * giomap_req_release:
 * ------------------
 * 	Release passed in xfer_q element back to the available list for future
 * 	user requests.
 * 	Called from system context and uses no locks. Awakens any blocked user
 * 	request by signalling xfer_event
 */
STATIC void
giomap_req_release(
	giomap_queue_t	*qelem )
{
	giomap_region_data_t	*rdata = (qelem->cbp)->context;

	FUNC_TRACE(giomap_req_release);

	qelem->status = UDI_OK;

	UDI_QUEUE_REMOVE( &qelem->Q );
	rdata->xfer_inuse_q.numelem--;
	_OSDEP_ASSERT( qelem->Q.next != NULL );
	UDI_ENQUEUE_TAIL( &rdata->xfer_q.Q, &qelem->Q );
	rdata->xfer_q.numelem++;
	/* Signal any blocked process that there's a new queue element */
	_OSDEP_EVENT_SIGNAL( &rdata->xfer_q_event );	
}


/*
 * ---------------------------------------------------------------------------
 * Interface to common giomap code
 * ---------------------------------------------------------------------------
 */

/*
 * giomap_OS_init:
 * --------------
 * Called on first per-instance driver instantiation. We need to initialise
 * the OS-dependent structures in the region-local data. This routine is 
 * synchronous.
 *
 * Initialise OS-specific members of the region data area.
 * This is a non-blocking synchronous routine.
 */
STATIC void
giomap_OS_init( 
	giomap_region_data_t *rdata )
{
	static int	first_time = 1;

	FUNC_TRACE(giomap_OS_init);
#if defined(DEBUG)
	DEBUGPRINT(("giomap_OS_init: rdata = %p\n", rdata));
#endif	/* DEBUG */

	/*
	 * Allocate the control blocks that we're going to use for our
	 * internal xfer_cb and ioc_cb source. We need to allocate queue
	 * elements to hold the CB references and these will get moved from
	 * the available queue (xfer_q, ioc_q) to the in-use queue when the
	 * request is submitted.
	 *
	 * On completion of a request, we can awaken the correct process by
	 * ensuring that the transaction context references the queue element
	 * of the originating request.
	 */
	UDI_QUEUE_INIT( &rdata->xfer_q.Q );
	UDI_QUEUE_INIT( &rdata->xfer_inuse_q.Q );
#if 0
	UDI_QUEUE_INIT( &rdata->abort_q.Q );
	UDI_QUEUE_INIT( &rdata->abort_inuse_q.Q );
#endif

	/* General purpose allocation token queue */
	UDI_QUEUE_INIT( &rdata->alloc_q.Q );

	/* Initialise the queue-specific mutex's -- needed for MP systems */
	_OSDEP_MUTEX_INIT( &rdata->xfer_lock, "giomap_posix: Transfer Q lock" );
#if 0
	_OSDEP_MUTEX_INIT( &rdata->abort_lock, "giomap_posix: Abort Q lock" );
#endif

	_OSDEP_EVENT_INIT( &rdata->xfer_q_event );
#if 0
	_OSDEP_EVENT_INIT( &rdata->abort_q_event );
#endif

	/*
	 * Initialise the giomap_bufsize variable from the maximum_safe_alloc
	 * size in the init_context limits field
	 */
	if ( rdata->init_context.limits.max_safe_alloc > 0 ) {

		rdata->giomap_bufsize = 
			MY_MIN( rdata->init_context.limits.max_safe_alloc,
			       GIOMAP_BUFSIZE );
	} else {
		rdata->giomap_bufsize = GIOMAP_BUFSIZE;
	}

	/*
	 * Initialise the modname queue used to keep track of when its safe to
	 * remove a particular driver from the OS namespace
	 * NOTE: This routine is called for every region created but the
	 *	 giomap_mod_Q is _global_ data. Only initialise it once.
	 */
	if ( first_time ) {
		first_time = 0;
		UDI_QUEUE_INIT( &giomap_mod_Q );
	}

	return;
}

/*
 * giomap_OS_deinit:
 * ----------------
 * Called just before the region is torn down (from udi_final_cleanup_req).
 * This needs to release any resources which were allocated by giomap_OS_init
 * Currently this is just the OSDEP_MUTEX_T and OSDEP_EVENT_T members
 *
 * Release any OS-specific members of the region data area
 * which were allocated by giomap_OS_init (e.g. MUTEXs).
 * This is a non-blocking routine.
 */
STATIC void
giomap_OS_deinit(
	giomap_region_data_t	*rdata )
{
	FUNC_TRACE(giomap_OS_deinit);
	_OSDEP_MUTEX_DEINIT( &rdata->xfer_lock );
	_OSDEP_EVENT_DEINIT( &rdata->xfer_q_event );

}

/*
 * giomap_OS_bind_done:
 * -------------------
 * Called when the mapper <-> driver bind has been completed. At this point
 * the driver should become available for OS use. To do this we need to make
 * its entry-points refer to ours.
 * We do this by constructing a drvinfo_t structure based on intimate knowledge
 * of the driver name (from its static properties), and save this into our
 * OS-specific region of the giomap_region_data_t. We destroy the association
 * when we are explicitly UNBOUND (from the devmgmt_req code)
 *
 *          Called when the GIO bind has completed. The OS code
 *          should perform any initialisation required and then
 *          call udi_channel_event_complete with the passed
 *          parameters
 */
STATIC void
giomap_OS_bind_done(
	udi_channel_event_cb_t *cb,
	udi_status_t		status )
{
	giomap_region_data_t	*rdata = UDI_GCB(cb)->context;
	_udi_channel_t		*ch = (_udi_channel_t *)UDI_GCB(cb)->channel;
	const char		*modname;
	drvinfo_t		*mydrvinfo;
	giomap_mod_t		*modp;
	udi_boolean_t		is_random;

	FUNC_TRACE(giomap_OS_bind_done);

	/* Obtain the module name of the driver at the other end of the bind */
	modname=(const char *)ch->other_end->chan_region->reg_driver->drv_name;

	if ( status == UDI_OK ) {
		/*
		 * Determine if the device is random access (with size limits)
		 * or a sequential unlimited access device 
		 */
		is_random = (rdata->dev_size_lo != 0) || 
			    (rdata->dev_size_hi != 0);
		/* Construct a drvinfo_t for this driver */
		mydrvinfo = giomap_attach( modname, is_random );
		mydrvinfo->per_device_rdata = rdata;

		if ( mydrvinfo == NULL ) {
			/* We failed, so fail the bind */
			udi_channel_event_complete(cb, UDI_STAT_CANNOT_BIND);
		} else {
			/*
			 * Add this current modname to our internal list of
			 * bound drivers and increment the count. For the
			 * first driver bound we drv_attach() so that it
			 * gets our mapper entry points
			 */
			if ( (modp = giomap_find_modname( modname )) != NULL ) {
				/* Subsequent bind */
				_OSDEP_ASSERT(modp->nrefs >= 1);
				modp->nrefs++;
				MOD_INC_USE_COUNT;
				(void)giomap_detach( mydrvinfo );
				udi_channel_event_complete(cb, UDI_OK);
			} else {
				/* First bind */
				modp = _OSDEP_MEM_ALLOC( sizeof(giomap_mod_t),0,
							UDI_WAITOK);
				modp->modname = modname;
				modp->nrefs=1;
				modp->drvinfop = (void *)mydrvinfo;
				UDI_ENQUEUE_TAIL(&giomap_mod_Q, 
							(udi_queue_t *)modp);
				/*
				 * As soon as the driver is drv_attach'd we'll
				 * get a string of CFG_ADDs for each instance
				 */
#if defined (DEBUG)
				DEBUGPRINT(("giomap_OS_bind_done: drv_attach'ing %s\n",
				modname));
#endif	/* DEBUG */
				if (drv_attach( mydrvinfo ) == 0) {
					MOD_INC_USE_COUNT;
					udi_channel_event_complete(cb, UDI_OK);
				}
				else {
					_OSDEP_PRINTF("giomap_OS_bind_done: drv_attach failed\n");
					udi_channel_event_complete(cb, UDI_STAT_RESOURCE_UNAVAIL);
				}
			}
		}
	} else {
		_OSDEP_PRINTF("giomap_OS_bind_done: common gio mapper error status %d\n", status);
		udi_channel_event_complete(cb, status);
	}
}

/*
 * giomap_OS_unbind_done:
 * ---------------------
 *	Called when the udi_gio_unbind_req sequence has completed and before
 *	the common code responds to the UDI_DMGMT_UNBIND request. We need to
 *	remove the OS mapping if this is the last reference to the target
 *	driver.
 *
 *	Sleaze warning: this code assumes that the parent driver is still
 *	physically accessible. CHANGE THIS.
 *
 *          Called when the UDI_DMGMT_UNBIND operation has removed
 *          the parent-bind channel. The OS-specific code should
 *          release any bindings instantiated by giomap_OS_bind_done
 */
STATIC void
giomap_OS_unbind_done(
	udi_gio_bind_cb_t	*cb )
{
	/*giomap_region_data_t	*rdata = UDI_GCB(cb)->context;*/
	_udi_channel_t		*ch = (_udi_channel_t *)UDI_GCB(cb)->channel;
	const char		*modname;
	giomap_mod_t		*modp;

	FUNC_TRACE(giomap_OS_unbind_done);

	/* Obtain the module name of the driver at the other end of the bind */
	modname=(const char *)ch->other_end->chan_region->reg_driver->drv_name;

	modp = giomap_find_modname( modname );

	if ( modp ) {
		_OSDEP_ASSERT(modp->nrefs >= 1);
		modp->nrefs--;

		if ( modp->nrefs == 0 ) {
			/*
			 * Last driver instance, detach from OS
			 */
			UDI_QUEUE_REMOVE( (udi_queue_t *)modp );

			DEBUGPRINT(("giomap_OS_unbind_done: detaching %s\n",
				modname));
			(void)drv_detach( (drvinfo_t *)modp->drvinfop );
			(void)giomap_detach( modp->drvinfop );

			_OSDEP_MEM_FREE( modp );
		}
		MOD_DEC_USE_COUNT;
	}

	return;
}

/*
 * giomap_OS_io_done:
 * -----------------
 * Called on completion of an i/o request. Either from a user-buffer [ioctl]
 * or a kernel buffer [biostart]
 */
STATIC void
giomap_OS_io_done( 
	udi_gio_xfer_cb_t *gio_xfer_cb, 
	udi_status_t	   status )
{
	/*giomap_region_data_t	*rdata = UDI_GCB(gio_xfer_cb)->context;*/
	giomap_queue_t		*qelem;

	FUNC_TRACE(giomap_OS_io_done);

	qelem = (giomap_queue_t *)gio_xfer_cb->gcb.initiator_context;
	qelem->cbp = UDI_GCB(gio_xfer_cb);
	qelem->rw_cb = gio_xfer_cb;

#if LINUX_GIO_DEBUG
	printf("giomap_OS_io_done( cb = %p, status = %d )\n",
		gio_xfer_cb, status);
#endif /* DEBUG */

	qelem->status = status;

	/*
	 * Handle asynchronous ioctl() requests by only waking up synchronous
	 * ones
	 */
	switch ( qelem->type ) {
	case Ioctl:
		if ( !qelem->uio_p->u_async || !qelem->single_xfer ) {
			_OSDEP_EVENT_SIGNAL( &qelem->event );
		} else {
			/*
			 * Release queue element. The originating ioctl has
			 * long gone....
			 */
			giomap_req_release( qelem );
		}
		break;
	case Biostart:
		_OSDEP_EVENT_SIGNAL( &qelem->event );
		break;
	default:
		udi_assert((qelem->type == Ioctl) || (qelem->type == Biostart));
		break;
	}
	return;
}

/*
 * Called whenever a channel event indication is issued from the environment.
 * This provides a mechanism to handle constraints changing, and/or channels
 * closing. 
 *
 *          called on receipt of a udi_channel_event_ind
 */
STATIC void
giomap_OS_channel_event(
	udi_channel_event_cb_t *cb )
{
	FUNC_TRACE(giomap_OS_channel_event);

	/* TODO: Handle abrupt unbind */
	udi_channel_event_complete(cb, UDI_OK);
}

/*
 * giomap_OS_event:
 * ---------------
 * Called on receipt of an asynchronous event notification from the GIO
 * provider. This routine could initiate an async handler for the user 
 * application to inform it of the event. The contents of the event params
 * field is defined by the provider. We know nothing about it. Hopefully the
 * user application also understands the layout.
 * Once the user has been notified of the event, the event_cb needs to be
 * returned to the provider [via udi_gio_event_res]
 *
 *          called on receipt of a udi_gio_event_ind
 */
STATIC void
giomap_OS_event(
	udi_gio_event_cb_t	*cb)
{
	FUNC_TRACE(giomap_OS_event);
	udi_gio_event_res(cb);		/* Do nothing */
}

#if UNIXWARE
/*
 * Called when the generic code receives a UDI_DMGMT_UNBIND request. We need
 * to detach the mapped driver from the OS's knowledge
 */
STATIC void
giomap_OS_unbind(
	udi_mgmt_cb_t	*cb )
{
	/*giomap_region_data_t	*rdata = UDI_GCB(cb)->context;*/
	_udi_channel_t		*ch = (_udi_channel_t *)UDI_GCB(cb)->channel;
	const char		*modname;
	giomap_mod_t		*modp;

	/* Obtain driver name of parent, this should appear in our list of
	 * 'known' modnames. If it does, and there is only one reference to
	 * it, we can drv_detach the module and prevent further OS access to it
	 */
	FUNC_TRACE(giomap_OS_unbind);
	
	modname=(const char *)ch->other_end->chan_region->reg_driver->drv_name;

	if ( (modp = giomap_find_modname( modname )) != NULL ) {
		if ( modp->nrefs == 1 ) {
			/* drv_detach the previously attached drvinfo_t */
			drv_detach( (drvinfo_t *)modp->drvinfop );
			giomap_detach( modp->drvinfop );
		}
		modp->nrefs--;

		if ( modp->nrefs == 0 ) {
			/* Free up the modp queue element */
			UDI_QUEUE_REMOVE( (udi_queue_t *)modp );
			_OSDEP_MEM_FREE( modp );
		}
		MOD_DEC_USE_COUNT;
	}

	return;
}
#endif
/*
 * ===========================================================================
 * Internal resource allocation routines
 */

/*
 * Allocate queue elements and control blocks for use by biostart() and
 * ioctl() interfaces
 */
STATIC void
giomap_OS_Alloc_Resources(
	giomap_region_data_t	*rdata )
{
	FUNC_TRACE(giomap_OS_Alloc_Resources);
	udi_mem_alloc( _giomap_got_alloc_cb, UDI_GCB(rdata->my_bind_cb),
		      sizeof( udi_queue_t ), UDI_MEM_NOZERO );
}

/*
 * Free all previously allocated queue elements and control blocks
 */
STATIC void
giomap_OS_Free_Resources(
	giomap_region_data_t	*rdata )
{

	udi_ubit32_t	ii;
	giomap_queue_t	*qelem;
	udi_queue_t	*head, *elem, *tmp;

	FUNC_TRACE(giomap_OS_Free_Resources);

	for ( ii = 0x80000000; ii > 0; ii >>= 1 ) {
		if ( !(rdata->resources & ii) ) continue;
		switch ( ii ) {
		case GiomapR_Req_Mem:
			head = &rdata->xfer_q.Q;
			GIOMAP_ASSERT( !UDI_QUEUE_EMPTY( &rdata->xfer_q.Q ) );
			UDI_QUEUE_FOREACH(head, elem, tmp) {
				qelem = (giomap_queue_t *)elem;
				_OSDEP_EVENT_DEINIT( &qelem->event );
				udi_buf_free(qelem->rw_cb->data_buf);
				udi_cb_free(UDI_GCB(qelem->rw_cb));
				qelem->rw_cb = NULL;
				udi_buf_free(qelem->diag_cb->data_buf);
				udi_cb_free(UDI_GCB(qelem->diag_cb));
				qelem->diag_cb = NULL;
				UDI_QUEUE_REMOVE( elem );
				rdata->xfer_q.numelem--;
				udi_mem_free( qelem->kernbuf );
				qelem->kernbuf = NULL;
				udi_mem_free( qelem );
			}
			GIOMAP_ASSERT(UDI_QUEUE_EMPTY(&rdata->xfer_inuse_q.Q));
			break;
		default:
			break;
		}
		rdata->resources &= ~ii;
	}
}

STATIC void
_giomap_getnext_rsrc(
	giomap_region_data_t	*rdata )
{
	udi_ubit32_t	ii;
	udi_queue_t	*rq, *tmp;

	FUNC_TRACE(_giomap_getnext_rsrc);

	for ( ii = 1; ii < 0x80000000; ii <<= 1 ) {
		if ( rdata->resource_rqst & ii ) continue;
		if ( rdata->resources & ii ) continue;

		rq = UDI_DEQUEUE_HEAD(&rdata->alloc_q.Q);
		if ( rq == NULL ) return;
		rdata->alloc_q.numelem--;
		rdata->resource_rqst |= ii;
		switch ( ii ) {
		case GiomapR_Req_Mem:
			udi_mem_alloc( _giomap_got_reqmem,
					UDI_GCB(rdata->my_bind_cb),
					sizeof( giomap_queue_t ), 0);
			udi_mem_free( rq );
			break;
#if 0
		case GiomapR_Abort_Mem:
			udi_mem_alloc( _giomap_got_abortmem,
					UDI_GCB(rdata->my_bind_cb),
					sizeof( giomap_queue_t ), 0);
			udi_mem_free(rq);
			break;
#endif
		default:
			UDI_ENQUEUE_TAIL( &rdata->alloc_q.Q, rq );
			rdata->alloc_q.numelem++;
			rdata->resources |= ii;
			break;
		}
	}

	if ( rdata->resources == 0x7fffffff ) {
		udi_queue_t *head = &rdata->alloc_q.Q;
		UDI_QUEUE_FOREACH( head, rq, tmp ) {
			UDI_QUEUE_REMOVE( rq );
			udi_mem_free( rq );
			rdata->alloc_q.numelem--;
		}
		rdata->resources |= 0x80000000;
		giomap_Resources_Alloced( rdata );
	}
}


STATIC void
_giomap_got_reqmem(
	udi_cb_t	*gcb,
	void		*new_mem )
{
	giomap_region_data_t	*rdata = gcb->context;

	FUNC_TRACE(_giomap_got_reqmem);
#ifdef DEBUG
	_OSDEP_ASSERT(new_mem != NULL);
#endif
	UDI_ENQUEUE_TAIL( &rdata->xfer_q.Q, (udi_queue_t *)new_mem );
	rdata->xfer_q.numelem++;

	/*
	 * Allocate CBs for:
	 *	Read/Write 	[udi_gio_rw_params_t], 
	 *	Diagnostics	[udi_gio_diag_params_t] + User-specific,
	 */
	udi_cb_alloc( _giomap_got_req_RW_cb, gcb, UDI_GIO_XFER_CB_RW_IDX,
			UDI_GCB(rdata->my_bind_cb)->channel);
}

STATIC void
_giomap_got_req_RW_cb(
	udi_cb_t	*gcb,
	udi_cb_t	*new_cb )
{
	giomap_region_data_t	*rdata = gcb->context;
	giomap_queue_t		*qelem;

	FUNC_TRACE(_giomap_got_req_RW_cb);

	qelem = (giomap_queue_t *)UDI_LAST_ELEMENT( &rdata->xfer_q.Q );
	qelem->rw_cb = UDI_MCB( new_cb, udi_gio_xfer_cb_t );

	udi_cb_alloc( _giomap_got_req_DIAG_cb, gcb, UDI_GIO_XFER_CB_DIAG_IDX,
			UDI_GCB(rdata->my_bind_cb)->channel);
}

STATIC void
_giomap_got_req_DIAG_cb(
	udi_cb_t	*gcb,
	udi_cb_t	*new_cb )
{
	giomap_region_data_t	*rdata = gcb->context;
	giomap_queue_t		*qelem;

	FUNC_TRACE(_giomap_got_req_DIAG_cb);

	qelem = (giomap_queue_t *)UDI_LAST_ELEMENT( &rdata->xfer_q.Q );
	qelem->diag_cb = UDI_MCB( new_cb, udi_gio_xfer_cb_t );

	_OSDEP_EVENT_INIT( &qelem->event );
	qelem->diag_cb = UDI_MCB( new_cb, udi_gio_xfer_cb_t );

	/* Initialise cbp to the diag_cb */
	qelem->cbp = new_cb;
	qelem->cb_type = Diag;

	qelem->buf_p = &qelem->u.buf;
	qelem->uio_p = &qelem->u.uio;

	if ( rdata->xfer_q.numelem < GIOMAP_MAX_CBS ) {
		udi_mem_alloc( _giomap_got_reqmem, gcb, 
				sizeof( giomap_queue_t ), 0 );
	} else {
		rdata->resources |= GiomapR_Req_Mem;
		udi_mem_alloc( _giomap_got_alloc_cb, gcb, sizeof( udi_queue_t ),
				UDI_MEM_NOZERO );
	}
}

/*
 * Get the next resource - common to both xfer_q and abort_q
 */
STATIC void
_giomap_got_alloc_cb(
	udi_cb_t	*gcb,
	void		*new_mem )
{
	giomap_region_data_t	*rdata = gcb->context;

	FUNC_TRACE(_giomap_got_alloc_cb);

	UDI_ENQUEUE_TAIL(&rdata->alloc_q.Q, (udi_queue_t *)new_mem);
	rdata->alloc_q.numelem++;
	_giomap_getnext_rsrc( rdata );
}

#if 0
STATIC void
_giomap_got_abortmem(
	udi_cb_t	*gcb,
	void		*new_mem )
{
	giomap_region_data_t	*rdata = gcb->context;

	FUNC_TRACE(_giomap_got_abortmem);

	UDI_ENQUEUE_TAIL( &rdata->abort_q.Q, (udi_queue_t *)new_mem );
	rdata->abort_q.numelem++;
	udi_cb_alloc( _giomap_got_abortcb, gcb, UDI_GIO_ABORT_CB_IDX,
		rdata->bind_info.bind_channel);
}

STATIC void
_giomap_got_abortcb(
	udi_cb_t	*gcb,
	udi_cb_t	*new_cb )
{
	giomap_region_data_t	*rdata = gcb->context;
	giomap_queue_t		*qelem;

	FUNC_TRACE(_giomap_got_abortcb);

	qelem = (giomap_queue_t *)UDI_LAST_ELEMENT( &rdata->abort_q.Q );
	_OSDEP_EVENT_INIT( &qelem->event );
	qelem->cbp = new_cb;

	if ( rdata->abort_q.numelem < GIOMAP_MAX_CBS ) {
		udi_mem_alloc( _giomap_got_abortmem, gcb, 
				sizeof( giomap_queue_t ), 0 );
	} else {
		rdata->resources |= GiomapR_Abort_Mem;
		udi_mem_alloc( _giomap_got_alloc_cb, gcb, sizeof( udi_queue_t ),
				UDI_MEM_NOZERO );
	}
}
#endif

/*
 * giomap_calc_offsets:
 * -------------------
 *	Convert a block-offset to its corresponding byte offset representation.
 *	This may exceed 32bits, so the hi and lo arguments are updated
 *	appropriately.
 */
STATIC void
giomap_calc_offsets(
	udi_ubit32_t	block,
	udi_ubit32_t	blkoff,
	udi_ubit32_t	*hi,
	udi_ubit32_t	*lo )
{
	FUNC_TRACE(giomap_calc_offsets);

	if ( block >= GIOMAP_MAX_BLOCK ) {
		*lo = (block - GIOMAP_MAX_BLOCK) << GIOMAP_SEC_SHFT;
		*hi = (block / GIOMAP_MAX_BLOCK);
	} else {
		*lo = block << GIOMAP_SEC_SHFT;
		*hi = 0;
	}
	*lo += blkoff;
}

/*
 * giomap_find_modname:
 * -------------------
 *	Scan the list of already bound modules for <modname>. If found return
 *	a reference to the entry. Otherwise return NULL
 */
STATIC giomap_mod_t *
giomap_find_modname( const char *modname )
{
	udi_queue_t	*elem, *tmp, *head;
	giomap_mod_t	*modp;

	FUNC_TRACE(giomap_find_modname);

	head = &giomap_mod_Q;
	UDI_QUEUE_FOREACH( head, elem, tmp ) {
		modp = (giomap_mod_t *)elem;
		if ( udi_strcmp(modname, modp->modname) == 0 ) {
			return modp;
		}
	}
	return NULL;
}

/*
 * _giomap_adjust_amount:
 * ---------------------
 *	Return the legal size of data which can be submitted to the device.
 *	The originating request is also modifed (u_resid).
 */
STATIC udi_ubit32_t 
_giomap_adjust_amount(
	giomap_region_data_t	*rdata,
	giomap_queue_t		*qelem )
{
	udi_ubit32_t		curr_offset_lo, curr_offset_hi; 
	udi_ubit32_t		new_offset_lo, new_offset_hi;
	udi_ubit32_t		amount, orig_amount;
	udi_gio_xfer_cb_t	*xfer_cb;
	udi_gio_rw_params_t	*rwparams;

	FUNC_TRACE(_giomap_adjust_amount);

	xfer_cb = UDI_MCB( qelem->cbp, udi_gio_xfer_cb_t );
	rwparams = xfer_cb->tr_params;

	curr_offset_lo = rwparams->offset_lo;
	curr_offset_hi = rwparams->offset_hi;

	/*
	 * Adjust offset for data which has previously been transferred. This
	 * happens when the user requests a transfer size which is larger than
	 * the rdata->giomap_bufsize. In this case there is only one ioctl()
	 * call made which gets split into smaller requests. Since the offset
	 * won't be updated by the user we have to handle it here.
	 */
	if ( curr_offset_lo >= GIOMAP_MAX_OFFSET ) {
		curr_offset_lo += qelem->uio_p->u_count - qelem->prev_count;
		curr_offset_hi++;
	} else {
		curr_offset_lo += qelem->uio_p->u_count - qelem->prev_count;
	}

	/*
	 * Get amount of transfer request
	 */
	amount = qelem->single_xfer ? qelem->uio_p->u_resid :
		 rdata->giomap_bufsize;

	orig_amount = amount;

	new_offset_hi = curr_offset_hi;
	new_offset_lo = curr_offset_lo + amount;

	if ( new_offset_lo < curr_offset_lo ) {
		/* Wrapped into curr_offset_hi */
		new_offset_hi++;
	}

	/*
	 * Check to see that new_offset_hi:new_offset_lo doesn't exceed device
	 * size
	 */
	if ( new_offset_hi < rdata->dev_size_hi ) {
		/* Must be space */
	} else if ( new_offset_hi == rdata->dev_size_hi ) {
		/* Space iff new_offset_lo <= rdata->dev_size_lo */
		if ( new_offset_lo <= rdata->dev_size_lo ) {
			/* Sufficient space for request */
		} else {
			/* Space exhausted, compute dev_size - curr_offset */
			amount = rdata->dev_size_lo - curr_offset_lo;
		}
	} else {
		/* Space exhausted, compute dev_size - curr_offset */
		amount = rdata->dev_size_lo - curr_offset_lo;
	}

	/*
	 * Adjust the user's request to reflect the (possibly) modified transfer
	 * amount.
	 */
	qelem->uio_p->u_resid -= (orig_amount - amount);
	return amount;
		
}

/*
 * _giomap_fake_ack:
 * ----------------
 * Completion routine for udi_timer_start() called when a zero-length data
 * transfer is attempted. This routine simply completes the request by calling
 * giomap_OS_io_done.
 */
STATIC void
_giomap_fake_ack(
	udi_cb_t	*gcb )
{
	udi_gio_xfer_cb_t *xfer_cb = UDI_MCB( gcb, udi_gio_xfer_cb_t );
	FUNC_TRACE(_giomap_fake_ack);
	giomap_OS_io_done( xfer_cb, UDI_OK );
}


STATIC ssize_t
giomap_read (struct file *filp, char *buf, size_t len, loff_t *off) {
	buf_t fab;
	int result;
	ssize_t amount = 0;
	void *perdevdata;
	giomap_init_data_t *gdata;
	giomap_region_data_t *rdata;
	channel_t channel;
	loff_t fsize;
	size_t maxlen;


FUNC_TRACE(giomap_read);
	
	fab.b_resid = 0;
	fab.b_flags = GIOMAP_B_READ;
	fab.b_un.b_addr = (void*)buf;
	fab.b_blkoff = (*off & (u32)(~0));
	fab.b_blkno = (*off >> GIOMAP_SEC_SHFT);
#ifdef DEBUG
	DEBUGPRINT(("read: blkoff=%X, blkno=%X\n",fab.b_blkoff,fab.b_blkno));
#endif
	perdevdata = giomap_get_per_device_data(filp);
	if (perdevdata == NULL)
		return -EINVAL;

	
	/* adjust size so that it does not exceed the file size */
	gdata = (giomap_init_data_t*)perdevdata;
	rdata = (giomap_region_data_t*)gdata->rdata;

	fsize = ((loff_t)(rdata->dev_size_hi)) << 32 | rdata->dev_size_lo;

	/* Handle unlimited size devices [dev_size_hi == dev_size_lo == 0] */
	if (fsize != 0) {
		maxlen = (size_t) ( (fsize-(*off)) & (size_t)~0);
	
		if (len>maxlen)
	  		len=maxlen;
	}

	if (len==0) return 0;
	fab.b_bcount=len;  
	 
	channel = giomap_get_channel(filp);
	/* Paranoia check to make sure we're talking to the right device */
	GIOMAP_ASSERT( rdata->channel == channel );
	result = giomap_biostart(perdevdata, channel, &fab, &amount);

	if (result != 0)
		return -EINVAL;

	*off+=amount;
	return amount;
}

STATIC ssize_t
giomap_write (struct file *filp, const char *buf, size_t len, loff_t *off) {
	buf_t fab;
	int result;
	ssize_t amount = 0;
	void *perdevdata;
	channel_t channel;
	giomap_init_data_t	*gdata;
	giomap_region_data_t	*rdata;

FUNC_TRACE(giomap_write);
	fab.b_bcount = len;
	fab.b_resid = 0;
	fab.b_flags = GIOMAP_B_WRITE;
	fab.b_un.b_addr = (void*)buf;
	fab.b_blkoff = (*off & (u32)(~0));
	fab.b_blkno = (*off >> GIOMAP_SEC_SHFT);
	perdevdata = giomap_get_per_device_data(filp);
	channel = giomap_get_channel(filp);
	gdata = (giomap_init_data_t *)perdevdata;
	rdata = (giomap_region_data_t *)gdata->rdata;
	GIOMAP_ASSERT( rdata->channel == channel );
	if (perdevdata == NULL)
		return -EINVAL;
	else
		result = giomap_biostart(perdevdata, channel, &fab, &amount);
	if (result != 0)
		return -EINVAL;
	
	*off+=amount;
	return amount;
}

STATIC int
giomap_open(struct inode *inode, struct file *filp) {
	int result;
	channel_t channel;
	void *perdevdata;

FUNC_TRACE(giomap_open);
#ifndef DEBUG
	MOD_INC_USE_COUNT; /* If we OOPS, udiM_gio wont rmmod. */
#endif
	channel = giomap_get_channel(filp);
	perdevdata = giomap_get_per_device_data(filp);
	if (perdevdata == NULL)
	{
		_OSDEP_PRINTF("udi: Cannot open. per-device-data was NULL.\n");
		result = -EINVAL;
	}
	else {
		result = giomap_open_uw(perdevdata, &channel);
	}

	if (result) {
		_OSDEP_PRINTF("giomap_open_uw: error %d\n", result);
		result = -EINVAL;
	}

	return result;
}

STATIC int
giomap_release (struct inode *inode, struct file *filp) {
FUNC_TRACE(giomap_release);
#ifndef DEBUG
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

STATIC void
mapper_init()
{
}

STATIC void
mapper_deinit()
{
}

