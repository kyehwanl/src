/*	$OpenBSD: hpc.c,v 1.5 2012/04/08 22:08:25 miod Exp $	*/
/*	$NetBSD: hpc.c,v 1.66 2011/07/01 18:53:46 dyoung Exp $	*/
/*	$NetBSD: ioc.c,v 1.9 2011/07/01 18:53:47 dyoung Exp $	 */

/*
 * Copyright (c) 2003 Christopher Sekiya
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 2000 Soren S. Jorvang
 * Copyright (c) 2001 Rafal K. Boni
 * Copyright (c) 2001 Jason R. Thorpe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Combined driver for the HPC (High performance Peripheral Controller)
 * and IOC2 (I/O Controller) chips.
 *
 * It would theoretically be better to attach an IOC driver to HPC on
 * IOC systems (IP22/24/26/28), and attach the few onboard devices
 * which attach directly to HPC on IP20, to IOC. But since IOC depends
 * too much on HPC, the complexity this would introduce is not worth
 * the hassle.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <mips64/archtype.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <sgi/gio/gioreg.h>
#include <sgi/gio/giovar.h>

#include <sgi/hpc/hpcvar.h>
#include <sgi/hpc/hpcreg.h>
#include <sgi/hpc/iocreg.h>
#include <sgi/sgi/ip22.h>

#include <dev/ic/smc93cx6var.h>

struct hpc_device {
	const char *hd_name;
	bus_addr_t hd_base;
	bus_addr_t hd_devoff;
	bus_addr_t hd_dmaoff;
	int hd_irq;
	int hd_sysmask;
};

static const struct hpc_device hpc1_devices[] = {
	/* probe order is important for IP20 zs */

	{ "zs",		/* Indigo serial 0/1 duart 1 */
	  HPC_BASE_ADDRESS_0,
	  0x0d10, 0,
	  5,
	  HPCDEV_IP20 },

	{ "zs",		/* Indigo kbd/ms duart 0 */
	  HPC_BASE_ADDRESS_0,
	  0x0d00, 0,
	  5,
	  HPCDEV_IP20 },

	{ "sq",		/* Indigo onboard ethernet */
	  HPC_BASE_ADDRESS_0,
	  HPC1_ENET_DEVREGS, HPC1_ENET_REGS,
	  3,
	  HPCDEV_IP20 },
	
	{ "sq",		/* E++ GIO adapter slot 0 (Indigo) */
	  HPC_BASE_ADDRESS_1,
	  HPC1_ENET_DEVREGS, HPC1_ENET_REGS,
	  6,
	  HPCDEV_IP20 },

	{ "sq",		/* E++ GIO adapter slot 0 (Indy) */
	  HPC_BASE_ADDRESS_1,
	  HPC1_ENET_DEVREGS, HPC1_ENET_REGS,
	  16 + 6,
	  HPCDEV_IP24 }, 

	{ "sq",		/* E++ GIO adapter slot 1 (Indigo) */
	  HPC_BASE_ADDRESS_2,
	  HPC1_ENET_DEVREGS, HPC1_ENET_REGS,
	  6,
	  HPCDEV_IP20 },

	{ "sq",		/* E++ GIO adapter slot 1 (Indy/Challenge S) */
	  HPC_BASE_ADDRESS_2,
	  HPC1_ENET_DEVREGS, HPC1_ENET_REGS,
	  16 + 7,
	  HPCDEV_IP24 },

	{ "wdsc",	/* Indigo onboard SCSI */
	  HPC_BASE_ADDRESS_0,
	  HPC1_SCSI0_DEVREGS, HPC1_SCSI0_REGS,
	  2,
	  HPCDEV_IP20 },

	{ "wdsc",	/* GIO32 SCSI adapter slot 0 (Indigo) */
	  HPC_BASE_ADDRESS_1,
	  HPC1_SCSI0_DEVREGS, HPC1_SCSI0_REGS,
	  6,
	  HPCDEV_IP20 },

	{ "wdsc",	/* GIO32 SCSI adapter slot 0 (Indy) */
	  HPC_BASE_ADDRESS_1,
	  HPC1_SCSI0_DEVREGS, HPC1_SCSI0_REGS,
	  16 + 6,
	  HPCDEV_IP24 }, 

	{ "wdsc",	/* GIO32 SCSI adapter slot 1 (Indigo) */
	  HPC_BASE_ADDRESS_2,
	  HPC1_SCSI0_DEVREGS, HPC1_SCSI0_REGS,
	  6,
	  HPCDEV_IP20 },

	{ "wdsc",	/* GIO32 SCSI adapter slot 1 (Indy/Challenge S) */
	  HPC_BASE_ADDRESS_2,
	  HPC1_SCSI0_DEVREGS, HPC1_SCSI0_REGS,
	  16 + 7,
	  HPCDEV_IP24 },

	{ NULL,
	  0,
	  0, 0,
	  0,
	  0
	}
};

static const struct hpc_device hpc3_devices[] = {
	{ "zs",		/* serial 0/1 duart 0 */
	  HPC_BASE_ADDRESS_0,
	  /* XXX Magic numbers */
	  IOC_BASE + IOC_SERIAL_REGS, 0,
	  24 + 5,
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "pckbc",	/* Indigo2/Indy ps2 keyboard/mouse controller */
	  HPC_BASE_ADDRESS_0,
	  IOC_BASE + IOC_KB_REGS, 0,
	  24 + 4,
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "sq",		/* Indigo2/Indy/Challenge S/Challenge M onboard enet */
	  HPC_BASE_ADDRESS_0,
	  HPC3_ENET_DEVREGS, HPC3_ENET_REGS,
	  3,
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "sq",		/* Challenge S IOPLUS secondary ethernet */
	  HPC_BASE_ADDRESS_1,
	  HPC3_ENET_DEVREGS, HPC3_ENET_REGS,
	  0,
	  HPCDEV_IP24 },

	{ "wdsc",	/* Indigo2/Indy/Challenge S/Challenge M onboard SCSI */
	  HPC_BASE_ADDRESS_0,
	  HPC3_SCSI0_DEVREGS, HPC3_SCSI0_REGS,
	  1,
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "wdsc",	/* Indigo2/Challenge M secondary onboard SCSI */
	  HPC_BASE_ADDRESS_0,
	  HPC3_SCSI1_DEVREGS, HPC3_SCSI1_REGS,
	  2,
	  HPCDEV_IP22 },

	{ "haltwo",	/* Indigo2/Indy onboard audio */
	  HPC_BASE_ADDRESS_0,
	  HPC3_PBUS_CH0_DEVREGS, HPC3_PBUS_DMAREGS,
	  8 + 4,	/* really the HPC DMA complete interrupt */
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "pione",	/* Indigo2/Indy/Challenge S/Challenge M onboard pport */
	  HPC_BASE_ADDRESS_0,
	  IOC_BASE + IOC_PLP_REGS, 0,
	  5,
	  HPCDEV_IP22 | HPCDEV_IP24 },

	{ "panel",	/* Indy front panel */
	  HPC_BASE_ADDRESS_0,
	  IOC_BASE + IOC_PANEL, 0,
	  9,
	  HPCDEV_IP24 },

	{ NULL,
	  0,
	  0, 0,
	  0,
	  0
	}
};

struct hpc_softc {
	struct device		sc_dev;

	bus_addr_t		sc_base;

	bus_space_tag_t		sc_ct;
	bus_space_handle_t	sc_ch;
	bus_dma_tag_t		sc_dmat;

	struct timeout		sc_blink_tmo;

	struct mips_bus_space	sc_hpc_bus_space;
};

static struct hpc_values hpc1_values = {
	.revision =		1,
	.scsi0_regs =		HPC1_SCSI0_REGS,
	.scsi0_regs_size =	HPC1_SCSI0_REGS_SIZE,
	.scsi0_cbp =		HPC1_SCSI0_CBP,
	.scsi0_ndbp = 		HPC1_SCSI0_NDBP,
	.scsi0_bc =		HPC1_SCSI0_BC,
	.scsi0_ctl =		HPC1_SCSI0_CTL,
	.scsi0_gio =		HPC1_SCSI0_GIO,
	.scsi0_dev =		HPC1_SCSI0_DEV,
	.scsi0_dmacfg =		HPC1_SCSI0_DMACFG,
	.scsi0_piocfg =		HPC1_SCSI0_PIOCFG,
	.scsi1_regs =		0,
	.scsi1_regs_size =	0,
	.scsi1_cbp =		0,
	.scsi1_ndbp =		0,
	.scsi1_bc =		0,
	.scsi1_ctl =		0,
	.scsi1_gio =		0,
	.scsi1_dev =		0,
	.scsi1_dmacfg =		0,
	.scsi1_piocfg =		0,
	.enet_regs =		HPC1_ENET_REGS,
	.enet_regs_size =	HPC1_ENET_REGS_SIZE,
	.enet_intdelay =	HPC1_ENET_INTDELAY,
	.enet_intdelayval =	HPC1_ENET_INTDELAY_OFF,
	.enetr_cbp =		HPC1_ENETR_CBP,
	.enetr_ndbp =		HPC1_ENETR_NDBP,
	.enetr_bc =		HPC1_ENETR_BC,
	.enetr_ctl =		HPC1_ENETR_CTL,
	.enetr_ctl_active =	HPC1_ENETR_CTL_ACTIVE,
	.enetr_reset =		HPC1_ENETR_RESET,
	.enetr_dmacfg =		0,
	.enetr_piocfg =		0,
	.enetx_cbp =		HPC1_ENETX_CBP,
	.enetx_ndbp =		HPC1_ENETX_NDBP,
	.enetx_bc =		HPC1_ENETX_BC,
	.enetx_ctl =		HPC1_ENETX_CTL,
	.enetx_ctl_active =	HPC1_ENETX_CTL_ACTIVE,
	.enetx_dev =		0,
	.enetr_fifo =		HPC1_ENETR_FIFO,
	.enetr_fifo_size =	HPC1_ENETR_FIFO_SIZE,
	.enetx_fifo =		HPC1_ENETX_FIFO,
	.enetx_fifo_size =	HPC1_ENETX_FIFO_SIZE,
	.enet_devregs =		HPC1_ENET_DEVREGS,
	.enet_devregs_size =	HPC1_ENET_DEVREGS_SIZE,
	.pbus_fifo =		0,	
	.pbus_fifo_size =	0,
	.pbus_bbram =		0,
#define MAX_SCSI_XFER   (roundup(MAXPHYS, PAGE_SIZE))
	.scsi_dma_segs =       (MAX_SCSI_XFER / 4096),
	.scsi_dma_segs_size =	4096,
	.scsi_dma_datain_cmd = (HPC1_SCSI_DMACTL_ACTIVE | HPC1_SCSI_DMACTL_DIR),
	.scsi_dma_dataout_cmd =	HPC1_SCSI_DMACTL_ACTIVE,
	.scsi_dmactl_flush =	HPC1_SCSI_DMACTL_FLUSH,
	.scsi_dmactl_active =	HPC1_SCSI_DMACTL_ACTIVE,
	.scsi_dmactl_reset =	HPC1_SCSI_DMACTL_RESET
};

static struct hpc_values hpc3_values = {
	.revision =		3,
	.scsi0_regs =		HPC3_SCSI0_REGS,
	.scsi0_regs_size =	HPC3_SCSI0_REGS_SIZE,
	.scsi0_cbp =		HPC3_SCSI0_CBP,
	.scsi0_ndbp =		HPC3_SCSI0_NDBP,
	.scsi0_bc =		HPC3_SCSI0_BC,
	.scsi0_ctl =		HPC3_SCSI0_CTL,
	.scsi0_gio =		HPC3_SCSI0_GIO,
	.scsi0_dev =		HPC3_SCSI0_DEV,
	.scsi0_dmacfg =		HPC3_SCSI0_DMACFG,
	.scsi0_piocfg =		HPC3_SCSI0_PIOCFG,
	.scsi1_regs =		HPC3_SCSI1_REGS,
	.scsi1_regs_size =	HPC3_SCSI1_REGS_SIZE,
	.scsi1_cbp =		HPC3_SCSI1_CBP,
	.scsi1_ndbp =		HPC3_SCSI1_NDBP,
	.scsi1_bc =		HPC3_SCSI1_BC,
	.scsi1_ctl =		HPC3_SCSI1_CTL,
	.scsi1_gio =		HPC3_SCSI1_GIO,
	.scsi1_dev =		HPC3_SCSI1_DEV,
	.scsi1_dmacfg =		HPC3_SCSI1_DMACFG,
	.scsi1_piocfg =		HPC3_SCSI1_PIOCFG,
	.enet_regs =		HPC3_ENET_REGS,
	.enet_regs_size =	HPC3_ENET_REGS_SIZE,
	.enet_intdelay =	0,
	.enet_intdelayval =	0,
	.enetr_cbp =		HPC3_ENETR_CBP,
	.enetr_ndbp =		HPC3_ENETR_NDBP,
	.enetr_bc =		HPC3_ENETR_BC,
	.enetr_ctl =		HPC3_ENETR_CTL,
	.enetr_ctl_active =	HPC3_ENETR_CTL_ACTIVE,
	.enetr_reset =		HPC3_ENETR_RESET,
	.enetr_dmacfg =		HPC3_ENETR_DMACFG,
	.enetr_piocfg =		HPC3_ENETR_PIOCFG,
	.enetx_cbp =		HPC3_ENETX_CBP,
	.enetx_ndbp =		HPC3_ENETX_NDBP,
	.enetx_bc =		HPC3_ENETX_BC,
	.enetx_ctl =		HPC3_ENETX_CTL,
	.enetx_ctl_active =	HPC3_ENETX_CTL_ACTIVE,
	.enetx_dev =		HPC3_ENETX_DEV,
	.enetr_fifo =		HPC3_ENETR_FIFO,
	.enetr_fifo_size =	HPC3_ENETR_FIFO_SIZE,
	.enetx_fifo =		HPC3_ENETX_FIFO,
	.enetx_fifo_size =	HPC3_ENETX_FIFO_SIZE,
	.enet_devregs =		HPC3_ENET_DEVREGS,
	.enet_devregs_size =	HPC3_ENET_DEVREGS_SIZE,
	.pbus_fifo =		HPC3_PBUS_FIFO,
	.pbus_fifo_size =	HPC3_PBUS_FIFO_SIZE,
	.pbus_bbram =		HPC3_PBUS_BBRAM,
	.scsi_dma_segs =       (MAX_SCSI_XFER / 8192),
	.scsi_dma_segs_size =	8192,
	.scsi_dma_datain_cmd =	HPC3_SCSI_DMACTL_ACTIVE,
	.scsi_dma_dataout_cmd =(HPC3_SCSI_DMACTL_ACTIVE | HPC3_SCSI_DMACTL_DIR),
	.scsi_dmactl_flush =	HPC3_SCSI_DMACTL_FLUSH,
	.scsi_dmactl_active =	HPC3_SCSI_DMACTL_ACTIVE,
	.scsi_dmactl_reset =	HPC3_SCSI_DMACTL_RESET
};

int	hpc_match(struct device *, void *, void *);
void	hpc_attach(struct device *, struct device *, void *);
int	hpc_print(void *, const char *);

int	hpc_revision(struct hpc_softc *, struct gio_attach_args *);
int	hpc_submatch(struct device *, void *, void *);
int	hpc_power_intr(void *);
void	hpc_blink(void *);
void	hpc_blink_ioc(void *);
int	hpc_read_eeprom(int, bus_space_tag_t, bus_space_handle_t, uint8_t *,
	    size_t);

const struct cfattach hpc_ca = {
	sizeof(struct hpc_softc), hpc_match, hpc_attach
};

struct cfdriver hpc_cd = {
	NULL, "hpc", DV_DULL
};

void	hpc_space_barrier(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    bus_size_t, int);

int
hpc_match(struct device *parent, void *vcf, void *aux)
{
	struct gio_attach_args* ga = aux;
	uint32_t dummy;

	/* Make sure it's actually there and readable */
	if (guarded_read_4(PHYS_TO_XKPHYS(ga->ga_addr, CCA_NC), &dummy) == 0)
		return 1;

	return 0;
}

void
hpc_attach(struct device *parent, struct device *self, void *aux)
{
	struct hpc_softc *sc = (struct hpc_softc *)self;
	struct gio_attach_args* ga = aux;
	struct hpc_attach_args ha;
	const struct hpc_device *hd;
	struct hpc_values *hv;
	uint32_t dummy;
	uint32_t hpctype;
	int isonboard;
	int isioplus;
	int giofast;
	int sysmask = 0;

	sc->sc_base = ga->ga_addr;
	sc->sc_ct = ga->ga_iot;
	sc->sc_ch = PHYS_TO_XKPHYS(sc->sc_base, CCA_NC);
	sc->sc_dmat = ga->ga_dmat;

	switch (sys_config.system_type) {
	case SGI_IP20:
		sysmask = HPCDEV_IP20;
		break;
	case SGI_IP22:
	case SGI_IP26:
	case SGI_IP28:
		if (sys_config.system_subtype == IP22_INDIGO2)
			sysmask = HPCDEV_IP22;
		else
			sysmask = HPCDEV_IP24;
		break;
	};

	if ((hpctype = hpc_revision(sc, ga)) == 0) {
		printf(": could not identify HPC revision\n");
		return;
	}

	if (hpctype != 3)
		hpc_old = 1;

	/* force big-endian mode */
	if (hpctype == 15)
		bus_space_write_4(sc->sc_ct, sc->sc_ch, HPC1_BIGENDIAN, 0);

	/*
	 * All machines have only one HPC on the mainboard itself. ''Extra''
	 * HPCs require bus arbiter and other magic to run happily.
	 */
	isonboard = (sc->sc_base == HPC_BASE_ADDRESS_0);
	isioplus = (sc->sc_base == HPC_BASE_ADDRESS_1 && hpctype == 3 &&
	    sysmask == HPCDEV_IP24);
	
	printf(": SGI HPC%d%s (%s)\n", (hpctype ==  3) ? 3 : 1,
	    (hpctype == 15) ? ".5" : "", (isonboard) ? "onboard" :
	    (isioplus) ? "IOPLUS mezzanine" : "GIO slot");

	/*
	 * Configure the IOC.
	 */
	if (isonboard && sys_config.system_type != SGI_IP20) {
		/* Reset IOC */
		bus_space_write_4(sc->sc_ct, sc->sc_ch, IOC_BASE + IOC_RESET,
		    IOC_RESET_PARALLEL | IOC_RESET_PCKBC | IOC_RESET_EISA |
		    IOC_RESET_ISDN | IOC_RESET_LED_GREEN );

		/*
		 * Set the 10BaseT port to use UTP cable, set autoselect mode
		 * for the Ethernet interface (AUI vs. TP), set the two serial
		 * ports to PC mode.
		 */
		bus_space_write_4(sc->sc_ct, sc->sc_ch, IOC_BASE + IOC_WRITE,
		    IOC_WRITE_ENET_AUTO | IOC_WRITE_ENET_UTP |
		    IOC_WRITE_PC_UART2 | IOC_WRITE_PC_UART1);

		/* XXX: the firmware should have taken care of this already */
#if 0
		if (sys_config.system_subtype == IP22_INDY) {
			bus_space_write_4(sc->sc_ct, sc->sc_ch,
			    IOC_BASE + IOC_GCSEL, 0xff);
			bus_space_write_4(sc->sc_ct, sc->sc_ch,
			    IOC_BASE + IOC_GCREG, 0xff);
		}
#endif
	}

	/*
	 * Configure the bus arbiter appropriately.
	 *
	 * In the case of Challenge S, we must tell the IOPLUS board which
	 * DMA channel to use (we steal it from one of the slots). SGI allows
	 * an HPC1.5 in slot 1, in which case IOPLUS must use EXP0, or any
	 * other DMA-capable board in slot 0, which leaves us to use EXP1. Of
	 * course, this means that only one GIO board may use DMA.
	 *
	 * Note that this never happens on Indigo2.
	 */
	if (isioplus) {
		int arb_slot;

		if (guarded_read_4(PHYS_TO_XKPHYS(HPC_BASE_ADDRESS_2, CCA_NC),
		    &dummy) != 0)
			arb_slot = GIO_SLOT_EXP1;
		else
			arb_slot = GIO_SLOT_EXP0;

		if (gio_arb_config(arb_slot, GIO_ARB_LB | GIO_ARB_MST |
		    GIO_ARB_64BIT | GIO_ARB_HPC2_64BIT)) {
			printf("%s: failed to configure GIO bus arbiter\n",
			    sc->sc_dev.dv_xname);
			return;
		}

		printf("%s: using EXP%d's DMA channel\n",
		    sc->sc_dev.dv_xname,
		    (arb_slot == GIO_SLOT_EXP0) ? 0 : 1);

		bus_space_write_4(sc->sc_ct, sc->sc_ch,
		    HPC3_PBUS_CFGPIO_REGS, 0x0003ffff);

		if (arb_slot == GIO_SLOT_EXP0)
			bus_space_write_4(sc->sc_ct, sc->sc_ch,
			    HPC3_PBUS_CH0_DEVREGS, 0x20202020);
		else
			bus_space_write_4(sc->sc_ct, sc->sc_ch,
			    HPC3_PBUS_CH0_DEVREGS, 0x30303030);
	} else if (!isonboard) {
		int arb_slot;

		arb_slot = (sc->sc_base == HPC_BASE_ADDRESS_1) ?
		    GIO_SLOT_EXP0 : GIO_SLOT_EXP1;

		if (gio_arb_config(arb_slot, GIO_ARB_RT | GIO_ARB_MST)) {
			printf("%s: failed to configure GIO bus arbiter\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	}

	hpc_read_eeprom(hpctype, sc->sc_ct, sc->sc_ch,
	    ha.hpc_eeprom, sizeof(ha.hpc_eeprom));

	/*
	 * Build a proper bus_space_tag for child devices.
	 * We will inherit the parent bus_space_tag, except for the barrier
	 * function which needs to be implemented differently on HPC3.
	 */
	bcopy(sc->sc_ct, &sc->sc_hpc_bus_space, sizeof(struct mips_bus_space));
	if (hpctype == 3)
		sc->sc_hpc_bus_space._space_barrier = hpc_space_barrier;

	if (hpctype == 3) {
		hd = hpc3_devices;
		hv = &hpc3_values;

		if (isonboard) {
			if (sys_config.system_subtype == IP22_INDIGO2) {
				/* wild guess */
				giofast = 1;
			} else {
				/*
				 * According to IRIX hpc3.h, the fast GIO bit
				 * is active high, but the register value has
				 * been found to be 0xf8 on slow GIO systems
				 * and 0xf1 on fast ones, which tends to prove
				 * the opposite...
				 */
				if (bus_space_read_4(sc->sc_ct, sc->sc_ch,
				    IOC_BASE + IOC_GCREG) & IOC_GCREG_GIO_33MHZ)
					giofast = 0;
				else
					giofast = 1;
			}
		} else {
			/*
			 * XXX should IO+ Mezzanine use the same settings as
			 * XXX the onboard HPC3?
			 */
			giofast = 0;
		}
	} else {
		hd = hpc1_devices;
		hv = &hpc1_values;
		hv->revision = hpctype;
		giofast = 0;
	}
	for (; hd->hd_name != NULL; hd++) {
		if (!(hd->hd_sysmask & sysmask) || hd->hd_base != sc->sc_base)
			continue;

		ha.ha_name = hd->hd_name;
		ha.ha_devoff = hd->hd_devoff;
		ha.ha_dmaoff = hd->hd_dmaoff;
		ha.ha_irq = hd->hd_irq;

		ha.ha_st = &sc->sc_hpc_bus_space;
		ha.ha_sh = sc->sc_ch;
		ha.ha_dmat = sc->sc_dmat;
		ha.hpc_regs = hv;
		ha.ha_giofast = giofast;

		/*
		 * XXX On hpc@gio boards such as the E++, this will cause 
		 * XXX `wdsc not configured' messages (or sq on SCSI
		 * XXX boards. We need to move some device detection
		 * XXX in there, or figure out if there is a way to know
		 * XXX what is really connected.
		 */
		config_found_sm(self, &ha, hpc_print, hpc_submatch);
	}

	/*
	 * Attach the clock chip as well if on hpc0.
	 */
	if (isonboard) {
		if (sys_config.system_type == SGI_IP20) {
			ha.ha_name = "dpclock";
			ha.ha_devoff = HPC1_PBUS_BBRAM;
		} else {
			ha.ha_name = "dsclock";
			ha.ha_devoff = HPC3_PBUS_BBRAM;
		}
		ha.ha_dmaoff = 0;
		ha.ha_irq = -1;
		ha.ha_st = &sc->sc_hpc_bus_space;
		ha.ha_sh = sc->sc_ch;
		ha.ha_dmat = sc->sc_dmat;
		ha.hpc_regs = NULL;
		ha.ha_giofast = giofast;

		config_found_sm(self, &ha, hpc_print, hpc_submatch);

		if (sys_config.system_type == SGI_IP20) {
			timeout_set(&sc->sc_blink_tmo, hpc_blink, sc);
			hpc_blink(sc);
		} else {
			timeout_set(&sc->sc_blink_tmo, hpc_blink_ioc, sc);
			hpc_blink_ioc(sc);
		}
	}
}

/*
 * HPC revision detection isn't as simple as it should be. Devices probe
 * differently depending on their slots, but luckily there is only one
 * instance in which we have to decide the major revision (HPC1 vs HPC3).
 *
 * The HPC is found in the following configurations:
 *	o Indigo R4k
 * 		One on-board HPC1 or HPC1.5.
 * 		Up to two additional HPC1.5's in GIO slots 0 and 1.
 *	o Indy
 * 		One on-board HPC3.
 *		Up to two additional HPC1.5's in GIO slots 0 and 1.
 *	o Challenge S
 * 		One on-board HPC3.
 * 		Up to one additional HPC3 on the IOPLUS board (if installed).
 *		Up to one additional HPC1.5 in slot 1 of the IOPLUS board.
 *	o Indigo2, Challenge M
 *		One on-board HPC3.
 *
 * All we really have to worry about is the IP22 case.
 */
int
hpc_revision(struct hpc_softc *sc, struct gio_attach_args *ga)
{
	uint32_t reg;

	/* No hardware ever supported the last hpc base address. */
	if (ga->ga_addr == HPC_BASE_ADDRESS_3)
		return 0;

	switch (sys_config.system_type) {
	case SGI_IP20:
		if (guarded_read_4(PHYS_TO_XKPHYS(ga->ga_addr + HPC1_BIGENDIAN,
		    CCA_NC), &reg) != 0) {
			if (((reg >> HPC1_REVSHIFT) & HPC1_REVMASK) ==
			    HPC1_REV15)
				return 15;
			else
				return 1;
		}
		return 1;

	case SGI_IP22:
	case SGI_IP26:
	case SGI_IP28:
		/*
		 * If IP22, probe slot 0 to determine if HPC1.5 or HPC3. Slot 1
		 * must be HPC1.5.
		 */
		if (ga->ga_addr == HPC_BASE_ADDRESS_0)
			return 3;

		if (ga->ga_addr == HPC_BASE_ADDRESS_2)
			return 15;

		/*
		 * Probe for it. We use one of the PBUS registers. Note
		 * that this probe succeeds with my E++ adapter in slot 1
		 * (bad), but it appears to always do the right thing in
		 * slot 0 (good!) and we're only worried about that one
		 * anyhow.
		 */
		if (guarded_read_4(PHYS_TO_XKPHYS(ga->ga_addr +
		    HPC3_PBUS_CH7_BP, CCA_NC), &reg) != 0)
			return 15;
		else
			return 3;
	}

	return 0;
}

int
hpc_submatch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = (struct cfdata *)vcf;
	struct hpc_attach_args *ha = (struct hpc_attach_args *)aux;

	if (cf->cf_loc[0 /*HPCCF_OFFSET*/] != -1 &&
	    (bus_addr_t)cf->cf_loc[0 /*HPCCF_OFFSET*/] != ha->ha_devoff)
		return 0;

	return (*cf->cf_attach->ca_match)(parent, cf, aux);
}

int
hpc_print(void *aux, const char *pnp)
{
	struct hpc_attach_args *ha = aux;

	if (pnp)
		printf("%s at %s", ha->ha_name, pnp);

	printf(" offset 0x%08lx", ha->ha_devoff);
	if (ha->ha_irq >= 0)
		printf(" irq %d", ha->ha_irq);

	return UNCONF;
}

/*
 * bus_space_barrier() function for HPC3 (which have a write buffer)
 */
void
hpc_space_barrier(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offs,
    bus_size_t sz, int how)
{
	__asm__ __volatile__ ("sync" ::: "memory");

	/* just read a side-effect free register */
	(void)*(volatile uint32_t *)
	    PHYS_TO_XKPHYS(HPC_BASE_ADDRESS_0 + HPC3_INTRSTAT_40, CCA_NC);
}

void
hpc_blink(void *arg)
{
	struct hpc_softc *sc = arg;

	bus_space_write_1(sc->sc_ct, sc->sc_ch, HPC1_AUX_REGS,
	    bus_space_read_1(sc->sc_ct, sc->sc_ch, HPC1_AUX_REGS) ^
	      HPC1_AUX_CONSLED);

	timeout_add(&sc->sc_blink_tmo,
	    (((averunnable.ldavg[0] + FSCALE) * hz) >> (FSHIFT + 1)));
}

void
hpc_blink_ioc(void *arg)
{
	struct hpc_softc *sc = arg;
	uint32_t value;

	/* This is a bit odd.  To strobe the green LED, we have to toggle the
	   red control bit. */
	value = bus_space_read_4(sc->sc_ct, sc->sc_ch, IOC_BASE + IOC_RESET) &
	    0xff;
	value ^= IOC_RESET_LED_RED;
	bus_space_write_4(sc->sc_ct, sc->sc_ch, IOC_BASE + IOC_RESET, value);

	timeout_add(&sc->sc_blink_tmo,
	    (((averunnable.ldavg[0] + FSCALE) * hz) >> (FSHIFT + 1)));
}

/*
 * Read the eeprom associated with one of the HPC's.
 *
 * NB: An eeprom is not always present, but the HPC should be able to
 *     handle this gracefully. Any consumers should validate the data to
 *     ensure it's reasonable.
 */
int
hpc_read_eeprom(int hpctype, bus_space_tag_t t, bus_space_handle_t h,
    uint8_t *buf, size_t len)
{
	struct seeprom_descriptor sd;
	bus_space_handle_t bsh;
	bus_size_t offset;

	if (!len || len & 0x1)
		return (1);

	offset = (hpctype == 3) ? HPC3_EEPROM_DATA : HPC1_AUX_REGS;

	if (bus_space_subregion(t, h, offset, 1, &bsh) != 0)
		return (1);

	sd.sd_chip = C56_66;
	sd.sd_tag = t;
	sd.sd_bsh = bsh;
	sd.sd_regsize = 1;
	sd.sd_control_offset = 0;
	sd.sd_status_offset = 0;
	sd.sd_dataout_offset = 0;
	sd.sd_DI = 0x10;	/* EEPROM -> CPU */
	sd.sd_DO = 0x08;	/* CPU -> EEPROM */
	sd.sd_CK = 0x04;
	sd.sd_CS = 0x02;
	sd.sd_MS = 0;
	sd.sd_RDY = 0;

	if (read_seeprom(&sd, (uint16_t *)buf, 0, len / 2) != 1)
		return (1);

	bus_space_unmap(t, bsh, 1);

	return 0;
}
