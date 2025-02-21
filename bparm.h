/*
 * Copyright (c) 2021 TK Chia
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the developer(s) nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Definitions for the boot protocol for passing information from the UEFI
 * bootloader to the stage 2 bootloader.
 *
 * This header file needs to work in both 32-bit & 64-bit compilation modes.
 */

#ifndef H_BPARM
#define H_BPARM

#include <inttypes.h>
#include "common.h"

/* "PCID" boot data, describing a single PCI device. */
typedef struct __attribute__ ((packed))
{
  uint32_t pci_locn;			/* PCI segment, bus, device, fn. */
  uint32_t pci_id;			/* vendor & device id. */
  uint32_t class_if;			/* class, subclass, prog. IF, &
					   rev. id. */
  uint16_t rimg_seg;			/* real mode segment where option
					   ROM image is (or has been copied
					   to); 0 if no ROM image */
  uint16_t rimg_rt_seg;			/* final location of option ROM code
					   after initialization (for PCI 3+
					   compliant ROM images) */
  uint32_t rimg_sz;			/* ROM image size */
} bdat_pci_dev_t;

/*
 * "BMEM" boot data, describing base memory availability at boot time &
 * run time.
 */
typedef struct __attribute__ ((packed))
{
  uint16_t boottime_bmem_bot_seg;	/* real mode seg. for start of
					   base mem. avail. to stage 2 at
					   boot time; stage 2 can free up
					   the mem. at [0, boottime_bmem_bot
					   * PARA_SIZE - 1] once it consumes
					   boot params. */
  uint16_t runtime_bmem_top_seg;	/* real mode seg. for end of base
					   mem. avail. at run time */
} bdat_bmem_t;

/* "MRNG" boot data, describing a single memory address range at run time. */
typedef struct __attribute__ ((packed))
{
  uint64_t start;			/* starting physical address */
  uint64_t len;				/* siae of memory address range */
  uint32_t e820_type;			/* mem. type as in int 0x15, 0xe820 */
  uint32_t e820_ext_attr;		/* extended memory attributes as in
					   int 0x15, ax = 0xe820 */
  uint64_t uefi_attr;			/* more memory attributes (including
					   cacheability attributes) as in
					   UEFI BS->GetMemoryMap (...) */
} bdat_mem_range_t;

/*
 * "RSDP" boot data, saying how to find the ACPI Root System Description
 * Pointer (RSDP) structure.
 */
typedef struct __attribute__ ((packed))
{
  ptr64_t rsdp_phy_addr;		/* 64-bit physical address of RSDP */
  uint32_t rsdp_sz;			/* size of RSDP */
} bdat_rsdp_t;

/* Node type for linked list of boot parameters. */
struct __attribute__ ((packed)) bparm
{
  struct bparm *next;			/* pointer to next boot param. node */
#ifndef __x86_64__
  uint32_t reserved;
#endif
  uint32_t type;			/* "PCID", etc. */
  uint32_t size;			/* size of boot param. data only */
  union
  {					/* boot param. data */
    bdat_pci_dev_t pci_dev;
    bdat_bmem_t bmem;
    bdat_mem_range_t mem_range;
    bdat_rsdp_t rsdp;
  } u[];
};

typedef struct bparm bparm_t;

#define BP_PCID		MAGIC32('P', 'C', 'I', 'D')
#define BP_BMEM		MAGIC32('B', 'M', 'E', 'M')
#define BP_MRNG		MAGIC32('M', 'R', 'N', 'G')
#define BP_RSDP		MAGIC32('R', 'S', 'D', 'P')

#endif
