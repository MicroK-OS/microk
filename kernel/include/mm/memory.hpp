#pragma once
#include <stdint.h>
#include <init/kinfo.hpp>

void memset(void *start, uint8_t value, uint64_t num);

namespace MEM {
	void Init(KInfo *info);
}