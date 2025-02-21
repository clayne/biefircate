/*
 * Copyright (c) 2021--2022 TK Chia
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

#include <stdbool.h>
#include <string.h>
#include "stage1/stage1.h"
#include "pci-common.h"

static bool
enable_legacy_vga (EFI_PCI_IO_PROTOCOL * io, UINT32 class_if,
		   UINT64 attrs, UINT64 supports, UINT64 * p_enables)
{
  EFI_STATUS status;
  UINT64 enables;
  *p_enables = 0;
  switch (class_if)
    {
    case PCI_CIF_VID_VGA:
    case PCI_CIF_VID_8514:
    case PCI_CIF_VID_XGA:
      break;
    default:
      return false;
    }
  switch (supports & (EFI_PCI_ATTRIBUTE_VGA_MEMORY
		      |EFI_PCI_ATTRIBUTE_VGA_IO | EFI_PCI_ATTRIBUTE_VGA_IO_16))
    {
    case EFI_PCI_ATTRIBUTE_VGA_MEMORY | EFI_PCI_ATTRIBUTE_VGA_IO:
      enables = EFI_PCI_ATTRIBUTE_VGA_MEMORY | EFI_PCI_ATTRIBUTE_VGA_IO;
      break;
    case EFI_PCI_ATTRIBUTE_VGA_MEMORY | EFI_PCI_ATTRIBUTE_VGA_IO_16:
    case EFI_PCI_ATTRIBUTE_VGA_MEMORY | EFI_PCI_ATTRIBUTE_VGA_IO_16
	 | EFI_PCI_ATTRIBUTE_VGA_IO:
      enables = EFI_PCI_ATTRIBUTE_VGA_MEMORY | EFI_PCI_ATTRIBUTE_VGA_IO_16;
      break;
    default:
      return false;
    }
  /*
   * If legacy VGA memory & I/O are already enabled, then there is
   * nothing more to do.
   */
  if ((attrs | enables) == attrs)
    return true;
  /* Otherwise... */
  status = io->Attributes (io, EfiPciIoAttributeOperationEnable,
			   enables, NULL);
  if (EFI_ERROR (status))
    return false;
  *p_enables = enables;
  return true;
}

const rimg_pcir_t *
rimg_find_pcir (const void *rimg, uint64_t rom_sz)
{
  const rimg_hdr_t *hdr = rimg;
  uint16_t pcir_off, pcir_sz, rimg_sz_hkib;
  uint64_t rimg_sz;
  const rimg_pcir_t *pcir;
  if (rom_sz < HKIBYTE)
    return NULL;
  if (hdr->sig != 0xaa55U)
    return NULL;
  pcir_off = hdr->pcir_off;
  if (!pcir_off)
    return NULL;
  if (pcir_off > rom_sz - PCIR_MIN_SZ)
    return NULL;
  pcir = (const rimg_pcir_t *) ((const char *) rimg + pcir_off);
  if (pcir->sig != PCIR_SIG_PCIR)
    return NULL;
  pcir_sz = pcir->pcir_sz;
  if (pcir_sz < PCIR_MIN_SZ || pcir_sz > rom_sz - pcir_off)
    return NULL;
  rimg_sz_hkib = pcir->rimg_sz_hkib;
  if (!rimg_sz_hkib || rimg_sz_hkib > rom_sz / HKIBYTE)
    return NULL;
  rimg_sz = rimg_sz_hkib * HKIBYTE;
  if (pcir_off > rimg_sz - PCIR_MIN_SZ || pcir_sz > rimg_sz - pcir_off)
    return NULL;
  if (compute_cksum (rimg, rimg_sz) != 0)
    return NULL;
  return pcir;
}

const uint16_t *
rimg_pcir_find_dev_id_list (const rimg_pcir_t * pcir, const void *rimg_end)
{
  /* FIXME: too much casting... */
  const uint8_t *dev_ids, *p;
  if (pcir->pcir_rev < 3)
    return NULL;
  if (!pcir->dev_ids_off)
    return NULL;
  dev_ids = p = (const uint8_t *) pcir + pcir->dev_ids_off;
  do
    {
      if (p >= (const uint8_t *) rimg_end
	  || p + 1 >= (const uint8_t *) rimg_end)
	{
	  warn (u"ROM image's Device List Pointer overshoots "
		"ROM end!  ignoring");
	  return NULL;
	}
      p += 2;
    }
  while (p[-2] != 0 || p[-1] != 0);
  return (const uint16_t *) dev_ids;
}

static uint64_t
rimg_find (const void *rimg)
{
  const rimg_hdr_t *hdr = rimg;
  uint8_t rimg_sz_hkib;
  uint64_t rimg_sz;
  if (hdr->sig != 0xaa55U)
    return 0;
  rimg_sz_hkib = hdr->rimg_sz_hkib_legacy;
  if (!rimg_sz_hkib || rimg_sz_hkib > 0x7f)
    return 0;
  rimg_sz = rimg_sz_hkib * HKIBYTE;
  if (compute_cksum (rimg, rimg_sz) != 0)
    return 0;
  return rimg_sz;
}

static void
get_rimg (bdat_pci_dev_t * bd, const void *rimg, uint32_t sz,
	  const rimg_pcir_t * pcir)
{
  void *rimg_copy, *rimg_rt;
  bd->rimg_sz = sz;
  if (!pcir)
    {
      infof (u"    ROM img.: @0x%lx~@0x%lx (no PCIR!)\r\n",
	     rimg, (char *) rimg + sz - 1);
      bd->rimg_seg = bd->rimg_rt_seg = ptr_to_rm_seg (rimg);
    }
  else if (pcir->pcir_rev >= 3)
    {
      uint32_t rt_sz = pcir->max_rt_sz_hkib * HKIBYTE;
      if (rt_sz != sz)
	{
	  if ((uintptr_t) rimg <= BMEM_MAX_ADDR - sz &&
	      (uintptr_t) rimg % HKIBYTE == 0)
	    {
	      infof (u"    ROM img.: @0x%lx~@0x%lx\r\n",
		     rimg, (char *) rimg + sz - 1);
	      bd->rimg_seg = ptr_to_rm_seg (rimg);
	      bd->rimg_sz = sz;
	    }
	  else
	    {
	      rimg_copy = bmem_alloc_boottime (sz, HKIBYTE);
	      memcpy (rimg_copy, rimg, sz);
	      infof (u"    ROM img.: @0x%lx~@0x%lx "
		     "(copied from @0x%lx)",
		     rimg_copy, (char *) rimg_copy + sz - 1, rimg);
	      bd->rimg_seg = ptr_to_rm_seg (rimg_copy);
	    }
	  /* FIXME: should run time addr. be 2 KiB aligned? */
	  rimg_rt = bmem_alloc (rt_sz, HKIBYTE);
	  infof (u"  run time: @0x%lx\r\n", rimg_rt);
	  bd->rimg_rt_seg = ptr_to_rm_seg (rimg_rt);
	  return;
	}
      /* or else fall through */
    }
  rimg_copy = bmem_alloc (sz, 2 * KIBYTE);
  memcpy (rimg_copy, rimg, sz);
  infof (u"    ROM img.: @0x%lx~@0x%lx (copied from @0x%lx)\r\n",
	 rimg_copy, (char *) rimg_copy + sz - 1, rimg);
  bd->rimg_seg = bd->rimg_rt_seg = ptr_to_rm_seg (rimg_copy);
}

static void
get_rimg_from_file (bdat_pci_dev_t * bd)
{
  bd->rimg_seg = bd->rimg_rt_seg = bd->rimg_sz = 0;
}

static void
get_rimg_from_pci_io (bdat_pci_dev_t * bd, EFI_PCI_IO_PROTOCOL * io)
{
  void *rimg = io->RomImage;
  uint64_t rsz = io->RomSize;
  uint32_t isz;
  const rimg_pcir_t *pcir;
  bd->rimg_seg = bd->rimg_rt_seg = bd->rimg_sz = 0;
  if (!rsz || !rimg)
    return;
  pcir = rimg_find_pcir (rimg, rsz);
  if (!pcir)
    return;
  if (pcir->type != PCIR_TYP_PCAT)
    {
      infof (u"    ROM img. via EFI_PCI_IO_PROTOCOL not "
	     "PC-AT compatible\r\n");
      return;
    }
  isz = (uint64_t) pcir->rimg_sz_hkib * HKIBYTE;
  get_rimg (bd, rimg, isz, pcir);
}

static void
get_rimg_from_fvs (bdat_pci_dev_t * bd)
{
  void *rimg;
  uint32_t sz;
  bd->rimg_seg = bd->rimg_rt_seg = bd->rimg_sz = 0;
  if (fv_find_rimg (bd->pci_id, bd->class_if, &rimg, &sz))
    {
      const rimg_pcir_t *pcir = rimg_find_pcir (rimg, sz);
      if (pcir)
	get_rimg (bd, rimg, sz, pcir);
    }
}

static void
get_rimg_special_case (bdat_pci_dev_t * bd)
{
  if (bd->pci_id == pci_make_id (PCI_VENDOR_ID_VBOX, PCI_DEVICE_ID_VBOX_VESA))
    {
      const rimg_hdr_t *rimg = (const rimg_hdr_t *) 0xc0000;
      uint64_t rimg_sz = rimg_find (rimg);
      const rimg_pcir_t *pcir;
      if (!rimg_sz)
	return;
      /*
       * This may be NULL!  The VirtualBox "graphics card" has a
       * PCI id., but its option ROM has no "PCIR" structure. =_=
       */
      pcir = rimg_find_pcir (rimg, rimg_sz);
      get_rimg (bd, rimg, rimg_sz, pcir);
    }
}

static bdat_pci_dev_t *
process_one_pci_io (EFI_PCI_IO_PROTOCOL * io, bool try_enable_vga)
{
  UINTN seg, bus, dev, fn;
  UINT64 attrs, supports, enables;
  UINT32 pci_conf[6];		/* buffer for PCI id. etc., & for BARs */
  UINT32 pci_id, class_if;
  bool got_bar = false;
  unsigned idx;
  bdat_pci_dev_t *bd, *vga = NULL;
  EFI_STATUS status = io->GetLocation (io, &seg, &bus, &dev, &fn);
  if (EFI_ERROR (status))
    error_with_status (u"cannot get PCI ctrlr. locn.", status);
  status = io->Attributes (io, EfiPciIoAttributeOperationGet, 0, &attrs);
  if (EFI_ERROR (status))
    error_with_status (u"cannot get PCI ctrlr. attrs.", status);
  status = io->Attributes (io, EfiPciIoAttributeOperationSupported,
			   0, &supports);
  if (EFI_ERROR (status))
    error_with_status (u"cannot get PCI ctrlr. attrs.", status);
  status = io->Pci.Read (io, EfiPciIoWidthUint32, 0, 4, pci_conf);
  if (EFI_ERROR (status))
    error_with_status (u"cannot read PCI conf. sp.", status);
  pci_id = pci_conf[0];
  class_if = pci_conf[2] & 0xffffff00U;
  infof (u"  %04x:%02x:%02x.%x %04x:%04x %02x %02x %02x 0x%06lx%c "
	  "0x%06lx%c 0x%06lx%c",
	 seg, bus, dev, fn,
	 (UINT32) pci_id_vendor (pci_id), (UINT32) pci_id_dev (pci_id),
	 class_if >> 24, (class_if >> 16) & 0xffU,
	 (class_if >> 8) & 0xffU,
	 io->RomSize <= 0xffffffULL ? io->RomSize : 0xffffffULL,
	 io->RomSize <= 0xffffffULL ? u' ' : u'+',
	 supports & 0xffffffULL,
	 supports & ~0xffffffULL ? u'+' : u' ',
	 attrs & 0xffffffULL, attrs & ~0xffffffULL ? u'+' : u' ');
  switch (class_if)
    {
    case PCI_CIF_VID_VGA:
      info (u" VGA");
      break;
    case PCI_CIF_VID_8514:
      info (u" 8514");
      break;
    case PCI_CIF_VID_XGA:
      info (u" XGA");
      break;
    case PCI_CIF_BUS_USB_UHCI:
      info (u" USB UHCI");
      break;
    case PCI_CIF_BUS_USB_OHCI:
      info (u" USB OHCI");
      break;
    case PCI_CIF_BUS_USB_EHCI:
      info (u" USB EHCI");
      break;
    case PCI_CIF_BUS_USB_XHCI:
      info (u" USB XHCI");
      break;
    }
  info (u"\r\n");
  /* Skip further processing if this is not a general device. */
  if ((pci_conf[3] >> 8 & 0xff) != 0)
    return false;
  /* Add a boot parameter for this PCI device. */
  bd = bparm_add (BP_PCID, sizeof (bdat_pci_dev_t));
  bd->pci_locn = seg << 16 | bus << 8 | dev << 3 | fn;
  bd->pci_id = pci_id;
  bd->class_if = class_if;
  get_rimg_from_file (bd);
  if (!bd->rimg_seg)
    {
      get_rimg_from_pci_io (bd, io);
      if (!bd->rimg_seg)
	{
	  get_rimg_from_fvs (bd);
	  if (!bd->rimg_seg)
	    get_rimg_special_case (bd);
	}
    }
  /*
   * If this is a VGA or XGA display controller, & there is an option
   * ROM for it, try to enable the legacy memory & I/O port locations
   * for the controller.
   */
  if (bd->rimg_seg)
    {
      if (try_enable_vga &&
	  enable_legacy_vga (io, class_if, attrs, supports, &enables))
	{
	  vga = bd;
	  attrs |= enables;
	  infof (u"    attrs. now 0x%lx\r\n", attrs);
	}
    }
  /* Enumerate all BAR values. */
  status = io->Pci.Read (io, EfiPciIoWidthUint32, 4 * sizeof (UINT32),
			 6, pci_conf);
  if (EFI_ERROR (status))
    error_with_status (u"cannot read PCI conf. sp.", status);
  idx = 0U - 1;
  while (++idx < 6)
    {
      UINT32 bar = pci_conf[idx];
      UINT64 addr;
      if (!bar)
	continue;
      if (!got_bar)
	{
	  got_bar = true;
	  info (u"    BAR:");
	}
      if (pci_bar_is_io (bar))
	infof (u" {\u2191" "0x%x}", pci_bar_addr (bar));
      else if (pci_bar_is_mem64 (bar))
	{
	  if (idx == 9)
	    error (u"bogus 64-bit PCI BAR");
	  ++idx;
	  addr = pci_conf[idx];
	  addr <<= 32;
	  addr |= pci_bar_addr (bar);
	  infof (u" {@0x%lx%s}", addr,
		 pci_bar_is_mempf (bar) ? u" pf" : u"");
	}
      else if (pci_bar_is_mem32 (bar))
	infof (u" {@0x%x%s}", pci_bar_addr (bar),
	       pci_bar_is_mempf (bar) ? u" pf" : u"");
      else
	error (u"unhandled 16-bit PCI BAR");
    }
  if (got_bar)
    info (u"\r\n");
  return vga;
}

/*
 * Go through all the PCI devices as reported by UEFI.  Try to see if any
 * devices have legacy option ROM images associated with them, & copy the
 * ROM images out to base memory.  Add boot parameters for the PCI devices
 * & their ROM images.
 */
void
process_pci (void)
{
  EFI_HANDLE *handles;
  UINTN num_handles, idx;
  bdat_pci_dev_t *vga = NULL;
  EFI_STATUS status = LibLocateHandle (ByProtocol,
				       &gEfiPciIoProtocolGuid, NULL,
				       &num_handles, &handles);
  if (EFI_ERROR (status) || !num_handles)
    error_with_status (u"no PCI devices found", status);
  infof (u"PCI devices: %lu\r\n"
	  "  locn.        PCI id.   class+IF ROM sz.   "
	  "supports  attrs.\r\n", num_handles);
  for (idx = 0; idx < num_handles; ++idx)
    {
      EFI_HANDLE handle = handles[idx];
      EFI_PCI_IO_PROTOCOL *io;
      status = BS->HandleProtocol (handle,
				   &gEfiPciIoProtocolGuid, (void **) &io);
      if (EFI_ERROR (status))
	error_with_status (u"cannot get EFI_PCI_IO_PROTOCOL", status);
      if (!vga)
	vga = process_one_pci_io (io, true);
      else
	process_one_pci_io (io, false);
    }
  FreePool (handles);
  if (!vga)
    error (u"no usable VGA/XGA controller?");
  if (!vga->rimg_seg)
    error (u"VGA/XGA device lacks option ROM?");
}
