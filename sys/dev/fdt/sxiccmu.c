/*	$OpenBSD: sxiccmu.c,v 1.11 2017/12/15 09:15:36 kettenis Exp $	*/
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2013 Artturi Alm
 * Copyright (c) 2016,2017 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#ifdef DEBUG_CCMU
#define DPRINTF(x)	do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

struct sxiccmu_ccu_bit {
	uint16_t reg;
	uint8_t bit;
	uint8_t parent;
};

#include "sxiccmu_clocks.h"

struct sxiccmu_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct sxiccmu_ccu_bit	*sc_gates;
	int			sc_ngates;
	struct clock_device	sc_cd;

	struct sxiccmu_ccu_bit	*sc_resets;
	int			sc_nresets;
	struct reset_device	sc_rd;

	uint32_t		(*sc_get_frequency)(struct sxiccmu_softc *,
				    uint32_t);
	int			(*sc_set_frequency)(struct sxiccmu_softc *,
				    uint32_t, uint32_t);
};

int	sxiccmu_match(struct device *, void *, void *);
void	sxiccmu_attach(struct device *, struct device *, void *);

struct cfattach	sxiccmu_ca = {
	sizeof (struct sxiccmu_softc), sxiccmu_match, sxiccmu_attach
};

struct cfdriver sxiccmu_cd = {
	NULL, "sxiccmu", DV_DULL
};

void sxiccmu_attach_clock(struct sxiccmu_softc *, int);

uint32_t sxiccmu_ccu_get_frequency(void *, uint32_t *);
int	sxiccmu_ccu_set_frequency(void *, uint32_t *, uint32_t);
void	sxiccmu_ccu_enable(void *, uint32_t *, int);
void	sxiccmu_ccu_reset(void *, uint32_t *, int);

uint32_t sxiccmu_a10_get_frequency(struct sxiccmu_softc *, uint32_t);
int	sxiccmu_a10_set_frequency(struct sxiccmu_softc *, uint32_t, uint32_t);
uint32_t sxiccmu_a64_get_frequency(struct sxiccmu_softc *, uint32_t);
int	sxiccmu_a64_set_frequency(struct sxiccmu_softc *, uint32_t, uint32_t);
uint32_t sxiccmu_a80_get_frequency(struct sxiccmu_softc *, uint32_t);
int	sxiccmu_a80_set_frequency(struct sxiccmu_softc *, uint32_t, uint32_t);
uint32_t sxiccmu_h3_get_frequency(struct sxiccmu_softc *, uint32_t);
int	sxiccmu_h3_set_frequency(struct sxiccmu_softc *, uint32_t, uint32_t);
uint32_t sxiccmu_nop_get_frequency(struct sxiccmu_softc *, uint32_t);
int	sxiccmu_nop_set_frequency(struct sxiccmu_softc *, uint32_t, uint32_t);

int
sxiccmu_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (node == OF_finddevice("/clocks")) {
		node = OF_parent(node);

		return (OF_is_compatible(node, "allwinner,sun4i-a10") ||
		    OF_is_compatible(node, "allwinner,sun5i-a10s") ||
		    OF_is_compatible(node, "allwinner,sun5i-r8") ||
		    OF_is_compatible(node, "allwinner,sun7i-a20") ||
		    OF_is_compatible(node, "allwinner,sun8i-h3") ||
		    OF_is_compatible(node, "allwinner,sun9i-a80") ||
		    OF_is_compatible(node, "allwinner,sun50i-a64") ||
		    OF_is_compatible(node, "allwinner,sun50i-h5"));
	}

	return (OF_is_compatible(node, "allwinner,sun4i-a10-ccu") ||
	    OF_is_compatible(node, "allwinner,sun7i-a20-ccu") ||
	    OF_is_compatible(node, "allwinner,sun8i-h3-ccu") ||
	    OF_is_compatible(node, "allwinner,sun9i-a80-ccu") ||
	    OF_is_compatible(node, "allwinner,sun9i-a80-usb-clks") ||
	    OF_is_compatible(node, "allwinner,sun9i-a80-mmc-config-clk") ||
	    OF_is_compatible(node, "allwinner,sun50i-a64-ccu") ||
	    OF_is_compatible(node, "allwinner,sun50i-h5-ccu"));
}

void
sxiccmu_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxiccmu_softc *sc = (struct sxiccmu_softc *)self;
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (faa->fa_nreg > 0 && bus_space_map(sc->sc_iot,
	    faa->fa_reg[0].addr, faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	if (OF_is_compatible(node, "allwinner,sun4i-a10-ccu") ||
	    OF_is_compatible(node, "allwinner,sun7i-a20-ccu")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun4i_a10_gates;
		sc->sc_ngates = nitems(sun4i_a10_gates);
		sc->sc_resets = sun4i_a10_resets;
		sc->sc_nresets = nitems(sun4i_a10_resets);
		sc->sc_get_frequency = sxiccmu_a10_get_frequency;
		sc->sc_set_frequency = sxiccmu_a10_set_frequency;
	} else if (OF_is_compatible(node, "allwinner,sun8i-h3-ccu") ||
	    OF_is_compatible(node, "allwinner,sun50i-h5-ccu")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun8i_h3_gates;
		sc->sc_ngates = nitems(sun8i_h3_gates);
		sc->sc_resets = sun8i_h3_resets;
		sc->sc_nresets = nitems(sun8i_h3_resets);
		sc->sc_get_frequency = sxiccmu_h3_get_frequency;
		sc->sc_set_frequency = sxiccmu_h3_set_frequency;
	} else if (OF_is_compatible(node, "allwinner,sun9i-a80-ccu")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun9i_a80_gates;
		sc->sc_ngates = nitems(sun9i_a80_gates);
		sc->sc_resets = sun9i_a80_resets;
		sc->sc_nresets = nitems(sun9i_a80_resets);
		sc->sc_get_frequency = sxiccmu_a80_get_frequency;
		sc->sc_set_frequency = sxiccmu_a80_set_frequency;
	} else if (OF_is_compatible(node, "allwinner,sun9i-a80-usb-clks")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun9i_a80_usb_gates;
		sc->sc_ngates = nitems(sun9i_a80_usb_gates);
		sc->sc_resets = sun9i_a80_usb_resets;
		sc->sc_nresets = nitems(sun9i_a80_usb_resets);
		sc->sc_get_frequency = sxiccmu_nop_get_frequency;
		sc->sc_set_frequency = sxiccmu_nop_set_frequency;
	} else if (OF_is_compatible(node, "allwinner,sun9i-a80-mmc-config-clk")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun9i_a80_mmc_gates;
		sc->sc_ngates = nitems(sun9i_a80_mmc_gates);
		sc->sc_resets = sun9i_a80_mmc_resets;
		sc->sc_nresets = nitems(sun9i_a80_mmc_resets);
		sc->sc_get_frequency = sxiccmu_nop_get_frequency;
		sc->sc_set_frequency = sxiccmu_nop_set_frequency;
	} else if (OF_is_compatible(node, "allwinner,sun50i-a64-ccu")) {
		KASSERT(faa->fa_nreg > 0);
		sc->sc_gates = sun50i_a64_gates;
		sc->sc_ngates = nitems(sun50i_a64_gates);
		sc->sc_resets = sun50i_a64_resets;
		sc->sc_nresets = nitems(sun50i_a64_resets);
		sc->sc_get_frequency = sxiccmu_a64_get_frequency;
		sc->sc_set_frequency = sxiccmu_a64_set_frequency;
	} else {
		for (node = OF_child(node); node; node = OF_peer(node))
			sxiccmu_attach_clock(sc, node);
	}

	if (sc->sc_gates) {
		sc->sc_cd.cd_node = sc->sc_node;
		sc->sc_cd.cd_cookie = sc;
		sc->sc_cd.cd_get_frequency = sxiccmu_ccu_get_frequency;
		sc->sc_cd.cd_set_frequency = sxiccmu_ccu_set_frequency;
		sc->sc_cd.cd_enable = sxiccmu_ccu_enable;
		clock_register(&sc->sc_cd);
	}

	if (sc->sc_resets) {
		sc->sc_rd.rd_node = sc->sc_node;
		sc->sc_rd.rd_cookie = sc;
		sc->sc_rd.rd_reset = sxiccmu_ccu_reset;
		reset_register(&sc->sc_rd);
	}
}

/*
 * Classic device trees for the Allwinner SoCs have basically a clock
 * node per register of the clock control unit.  Attaching a separate
 * driver to each of them would be crazy, so we handle them here.
 */

struct sxiccmu_clock {
	int sc_node;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	struct clock_device sc_cd;
	struct reset_device sc_rd;
};

struct sxiccmu_device {
	const char *compat;
	uint32_t (*get_frequency)(void *, uint32_t *);
	int	(*set_frequency)(void *, uint32_t *, uint32_t);
	void	(*enable)(void *, uint32_t *, int);
	void	(*reset)(void *, uint32_t *, int);
};

uint32_t sxiccmu_gen_get_frequency(void *, uint32_t *);
uint32_t sxiccmu_osc_get_frequency(void *, uint32_t *);
uint32_t sxiccmu_pll6_get_frequency(void *, uint32_t *);
void	sxiccmu_pll6_enable(void *, uint32_t *, int);
uint32_t sxiccmu_apb1_get_frequency(void *, uint32_t *);
uint32_t sxiccmu_cpus_get_frequency(void *, uint32_t *);
uint32_t sxiccmu_apbs_get_frequency(void *, uint32_t *);
int	sxiccmu_gmac_set_frequency(void *, uint32_t *, uint32_t);
int	sxiccmu_mmc_set_frequency(void *, uint32_t *, uint32_t);
void	sxiccmu_mmc_enable(void *, uint32_t *, int);
void	sxiccmu_gate_enable(void *, uint32_t *, int);
void	sxiccmu_reset(void *, uint32_t *, int);

struct sxiccmu_device sxiccmu_devices[] = {
	{
		.compat = "allwinner,sun4i-a10-osc-clk",
		.get_frequency = sxiccmu_osc_get_frequency,
	},
	{
		.compat = "allwinner,sun4i-a10-pll6-clk",
		.get_frequency = sxiccmu_pll6_get_frequency,
		.enable = sxiccmu_pll6_enable
	},
	{
		.compat = "allwinner,sun4i-a10-apb1-clk",
		.get_frequency = sxiccmu_apb1_get_frequency,
	},
	{
		.compat = "allwinner,sun4i-a10-ahb-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun4i-a10-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun4i-a10-apb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun4i-a10-mmc-clk",
		.set_frequency = sxiccmu_mmc_set_frequency,
		.enable = sxiccmu_mmc_enable
	},
	{
		.compat = "allwinner,sun4i-a10-usb-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable,
		.reset = sxiccmu_reset
	},
	{
		.compat = "allwinner,sun5i-a10s-ahb-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a10s-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a10s-apb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a13-ahb-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a13-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a13-apb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun5i-a13-usb-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable,
		.reset = sxiccmu_reset
	},
	{
		.compat = "allwinner,sun6i-a31-ahb1-reset",
		.reset = sxiccmu_reset
	},
	{
		.compat = "allwinner,sun6i-a31-clock-reset",
		.reset = sxiccmu_reset
	},
	{
		.compat = "allwinner,sun7i-a20-ahb-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun7i-a20-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun7i-a20-apb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun7i-a20-gmac-clk",
		.set_frequency = sxiccmu_gmac_set_frequency
	},
	{
		.compat = "allwinner,sun8i-a23-apb0-clk",
		.get_frequency = sxiccmu_apbs_get_frequency
	},
	{
		.compat = "allwinner,sun8i-h3-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-apb1-clk",
		.get_frequency = sxiccmu_apb1_get_frequency,
	},
	{
		.compat = "allwinner,sun9i-a80-ahb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-ahb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-ahb2-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-apb0-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-apb1-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-apbs-gates-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable
	},
	{
		.compat = "allwinner,sun9i-a80-cpus-clk",
		.get_frequency = sxiccmu_cpus_get_frequency
	},
	{
		.compat = "allwinner,sun9i-a80-mmc-clk",
		.set_frequency = sxiccmu_mmc_set_frequency,
		.enable = sxiccmu_mmc_enable
	},
	{
		.compat = "allwinner,sun9i-a80-usb-mod-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable,
		.reset = sxiccmu_reset
	},
	{
		.compat = "allwinner,sun9i-a80-usb-phy-clk",
		.get_frequency = sxiccmu_gen_get_frequency,
		.enable = sxiccmu_gate_enable,
		.reset = sxiccmu_reset
	},
};

void
sxiccmu_attach_clock(struct sxiccmu_softc *sc, int node)
{
	struct sxiccmu_clock *clock;
	uint32_t reg[2];
	int i;

	for (i = 0; i < nitems(sxiccmu_devices); i++)
		if (OF_is_compatible(node, sxiccmu_devices[i].compat))
			break;
	if (i == nitems(sxiccmu_devices))
		return;

	clock = malloc(sizeof(*clock), M_DEVBUF, M_WAITOK);
	clock->sc_node = node;

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) == sizeof(reg)) {
		clock->sc_iot = sc->sc_iot;
		if (bus_space_map(clock->sc_iot, reg[0], reg[1], 0,
		    &clock->sc_ioh)) {
			printf("%s: can't map registers", sc->sc_dev.dv_xname);
			free(clock, M_DEVBUF, sizeof(*clock));
			return;
		}
	}

	clock->sc_cd.cd_node = node;
	clock->sc_cd.cd_cookie = clock;
	clock->sc_cd.cd_get_frequency = sxiccmu_devices[i].get_frequency;
	clock->sc_cd.cd_set_frequency = sxiccmu_devices[i].set_frequency;
	clock->sc_cd.cd_enable = sxiccmu_devices[i].enable;
	clock_register(&clock->sc_cd);

	if (sxiccmu_devices[i].reset) {
		clock->sc_rd.rd_node = node;
		clock->sc_rd.rd_cookie = clock;
		clock->sc_rd.rd_reset = sxiccmu_devices[i].reset;
		reset_register(&clock->sc_rd);
	}
}

/*
 * A "generic" function that simply gets the clock frequency from the
 * parent clock.  Useful for clock gating devices that don't scale
 * their clocks.
 */
uint32_t
sxiccmu_gen_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;

	return clock_get_frequency(sc->sc_node, NULL);
}

uint32_t
sxiccmu_osc_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;

	return OF_getpropint(sc->sc_node, "clock-frequency", 24000000);
}

#define CCU_PLL6_ENABLE			(1U << 31)
#define CCU_PLL6_BYPASS_EN		(1U << 30)
#define CCU_PLL6_SATA_CLK_EN		(1U << 14)
#define CCU_PLL6_FACTOR_N(x)		(((x) >> 8) & 0x1f)
#define CCU_PLL6_FACTOR_N_MASK		(0x1f << 8)
#define CCU_PLL6_FACTOR_N_SHIFT		8
#define CCU_PLL6_FACTOR_K(x)		(((x) >> 4) & 0x3)
#define CCU_PLL6_FACTOR_K_MASK		(0x3 << 4)
#define CCU_PLL6_FACTOR_K_SHIFT		4
#define CCU_PLL6_FACTOR_M(x)		(((x) >> 0) & 0x3)
#define CCU_PLL6_FACTOR_M_MASK		(0x3 << 0)
#define CCU_PLL6_FACTOR_M_SHIFT		0

uint32_t
sxiccmu_pll6_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t reg, k, m, n, freq;
	uint32_t idx = cells[0];

	/* XXX Assume bypass is disabled. */
	reg = SXIREAD4(sc, 0);
	k = CCU_PLL6_FACTOR_K(reg) + 1;
	m = CCU_PLL6_FACTOR_M(reg) + 1;
	n = CCU_PLL6_FACTOR_N(reg);

	freq = clock_get_frequency_idx(sc->sc_node, 0);
	switch (idx) {
	case 0:	
		return (freq * n * k) / m / 6;		/* pll6_sata */
	case 1:	
		return (freq * n * k) / 2;		/* pll6_other */
	case 2:	
		return (freq * n * k);			/* pll6 */
	case 3:
		return (freq * n * k) / 4;		/* pll6_div_4 */
	}

	return 0;
}

void
sxiccmu_pll6_enable(void *cookie, uint32_t *cells, int on)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg;

	/* 
	 * Since this clock has several outputs, we never turn it off.
	 */

	reg = SXIREAD4(sc, 0);
	switch (idx) {
	case 0:			/* pll6_sata */
		if (on)
			reg |= CCU_PLL6_SATA_CLK_EN;
		else
			reg &= ~CCU_PLL6_SATA_CLK_EN;
		/* FALLTHROUGH */
	case 1:			/* pll6_other */
	case 2:			/* pll6 */
	case 3:			/* pll6_div_4 */
		if (on)
			reg |= CCU_PLL6_ENABLE;
	}
	SXIWRITE4(sc, 0, reg);
}

#define CCU_APB1_CLK_RAT_N(x)		(((x) >> 16) & 0x3)
#define CCU_APB1_CLK_RAT_M(x)		(((x) >> 0) & 0x1f)
#define CCU_APB1_CLK_SRC_SEL(x)		(((x) >> 24) & 0x3)

uint32_t
sxiccmu_apb1_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t reg, m, n, freq;
	int idx;

	reg = SXIREAD4(sc, 0);
	m = CCU_APB1_CLK_RAT_M(reg);
	n = CCU_APB1_CLK_RAT_N(reg);
	idx = CCU_APB1_CLK_SRC_SEL(reg);

	freq = clock_get_frequency_idx(sc->sc_node, idx);
	return freq / (1 << n) / (m + 1);
}

#define CCU_CPUS_CLK_SRC_SEL(x)		(((x) >> 16) & 0x3)
#define CCU_CPUS_POST_DIV(x)		(((x) >> 8) & 0x1f)
#define CCU_CPUS_CLK_RATIO(x)		(((x) >> 0) & 0x3)

uint32_t
sxiccmu_cpus_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t reg, post_div, clk_ratio, freq;
	int idx;

	reg = SXIREAD4(sc, 0);
	idx = CCU_CPUS_CLK_SRC_SEL(reg);
	post_div = (idx == 2 ? CCU_CPUS_POST_DIV(reg): 0);
	clk_ratio = CCU_CPUS_CLK_RATIO(reg);

	freq = clock_get_frequency_idx(sc->sc_node, idx);
	return freq / (clk_ratio + 1) / (post_div + 1);
}

#define CCU_APBS_CLK_RATIO(x)		(((x) >> 0) & 0x3)

uint32_t
sxiccmu_apbs_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t reg, freq;

	reg = SXIREAD4(sc, 0);
	freq = clock_get_frequency(sc->sc_node, NULL);
	return freq / (CCU_APBS_CLK_RATIO(reg) + 1);
}

#define	CCU_GMAC_CLK_PIT		(1 << 2)
#define	CCU_GMAC_CLK_TCS		(3 << 0)
#define	CCU_GMAC_CLK_TCS_MII		0
#define	CCU_GMAC_CLK_TCS_EXT_125	1
#define	CCU_GMAC_CLK_TCS_INT_RGMII	2

int
sxiccmu_gmac_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct sxiccmu_clock *sc = cookie;

	switch (freq) {
	case 25000000:		/* MMI, 25 MHz */
		SXICMS4(sc, 0, CCU_GMAC_CLK_PIT|CCU_GMAC_CLK_TCS,
		    CCU_GMAC_CLK_TCS_MII);
		break;
	case 125000000:		/* RGMII, 125 MHz */
		SXICMS4(sc, 0, CCU_GMAC_CLK_PIT|CCU_GMAC_CLK_TCS,
		    CCU_GMAC_CLK_PIT|CCU_GMAC_CLK_TCS_INT_RGMII);
		break;
	default:
		return -1;
	}

	return 0;
}

#define CCU_SDx_SCLK_GATING		(1U << 31)
#define CCU_SDx_CLK_SRC_SEL_OSC24M	(0 << 24)
#define CCU_SDx_CLK_SRC_SEL_PLL6	(1 << 24)
#define CCU_SDx_CLK_SRC_SEL_PLL5	(2 << 24)
#define CCU_SDx_CLK_SRC_SEL_MASK	(3 << 24)
#define CCU_SDx_CLK_DIV_RATIO_N_MASK	(3 << 16)
#define CCU_SDx_CLK_DIV_RATIO_N_SHIFT	16
#define CCU_SDx_CLK_DIV_RATIO_M_MASK	(7 << 0)
#define CCU_SDx_CLK_DIV_RATIO_M_SHIFT	0

int
sxiccmu_mmc_do_set_frequency(struct sxiccmu_clock *sc, uint32_t freq,
    uint32_t parent_freq)
{
	uint32_t reg, m, n;
	uint32_t clk_src;

	switch (freq) {
	case 400000:
		n = 2, m = 15;
		clk_src = CCU_SDx_CLK_SRC_SEL_OSC24M;
		break;
	case 25000000:
	case 26000000:
	case 50000000:
	case 52000000:
		n = 0, m = 0;
		clk_src = CCU_SDx_CLK_SRC_SEL_PLL6;
		while ((parent_freq / (1 << n) / 16) > freq)
			n++;
		while ((parent_freq / (1 << n) / (m + 1)) > freq)
			m++;
		break;
	default:
		return -1;
	}

	reg = SXIREAD4(sc, 0);
	reg &= ~CCU_SDx_CLK_SRC_SEL_MASK;
	reg |= clk_src;
	reg &= ~CCU_SDx_CLK_DIV_RATIO_N_MASK;
	reg |= n << CCU_SDx_CLK_DIV_RATIO_N_SHIFT;
	reg &= ~CCU_SDx_CLK_DIV_RATIO_M_MASK;
	reg |= m << CCU_SDx_CLK_DIV_RATIO_M_SHIFT;
	SXIWRITE4(sc, 0, reg);

	return 0;
}

int
sxiccmu_mmc_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct sxiccmu_clock *sc = cookie;
	uint32_t parent_freq;

	if (cells[0] != 0)
		return -1;

	parent_freq = clock_get_frequency_idx(sc->sc_node, 1);
	return sxiccmu_mmc_do_set_frequency(sc, freq, parent_freq);
}

void
sxiccmu_mmc_enable(void *cookie, uint32_t *cells, int on)
{
	struct sxiccmu_clock *sc = cookie;

	if (cells[0] != 0)
		return;

	if (on)
		SXISET4(sc, 0, CCU_SDx_SCLK_GATING);
	else
		SXICLR4(sc, 0, CCU_SDx_SCLK_GATING);
}

void
sxiccmu_gate_enable(void *cookie, uint32_t *cells, int on)
{
	struct sxiccmu_clock *sc = cookie;
	int reg = cells[0] / 32;
	int bit = cells[0] % 32;

	if (on) {
		clock_enable(sc->sc_node, NULL);
		SXISET4(sc, reg * 4, (1U << bit));
	} else {
		SXICLR4(sc, reg * 4, (1U << bit));
		clock_disable(sc->sc_node, NULL);
	}
}

void
sxiccmu_reset(void *cookie, uint32_t *cells, int assert)
{
	struct sxiccmu_clock *sc = cookie;
	int reg = cells[0] / 32;
	int bit = cells[0] % 32;

	if (assert)
		SXICLR4(sc, reg * 4, (1U << bit));
	else
		SXISET4(sc, reg * 4, (1U << bit));
}

/*
 * Newer device trees, such as those for the Allwinner H3/A64 have
 * most of the clock nodes replaced with a single clock control unit
 * node.
 */

uint32_t
sxiccmu_ccu_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxiccmu_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t parent;

	if (idx < sc->sc_ngates && sc->sc_gates[idx].parent) {
		parent = sc->sc_gates[idx].parent;
		return sxiccmu_ccu_get_frequency(sc, &parent);
	}

	return sc->sc_get_frequency(sc, idx);
}

/* Allwinner H3/A64 */
#define CCU_AHB1_APB1_CFG_REG		0x0054
#define CCU_AHB1_CLK_SRC_SEL		(3 << 12)
#define CCU_AHB1_CLK_SRC_SEL_LOSC	(0 << 12)
#define CCU_AHB1_CLK_SRC_SEL_OSC24M	(1 << 12)
#define CCU_AHB1_CLK_SRC_SEL_AXI	(2 << 12)
#define CCU_AHB1_CLK_SRC_SEL_PERIPH0	(3 << 12)
#define CCU_AHB1_PRE_DIV(x)		((((x) >> 6) & 3) + 1)
#define CCU_AHB1_CLK_DIV_RATIO(x)	(1 << (((x) >> 4) & 3))
#define CCU_AHB2_CFG_REG		0x005c
#define CCU_AHB2_CLK_CFG		(3 << 0)

uint32_t
sxiccmu_a10_get_frequency(struct sxiccmu_softc *sc, uint32_t idx)
{
	switch (idx) {
	case A10_CLK_PLL_PERIPH_BASE:
		/* Not hardcoded, but recommended. */
		return 600000000;
	case A10_CLK_PLL_PERIPH:
		return sxiccmu_a10_get_frequency(sc, A10_CLK_PLL_PERIPH_BASE) * 2;
	case A10_CLK_APB1:
		/* XXX Controlled by a MUX. */
		return 24000000;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

uint32_t
sxiccmu_a64_get_frequency(struct sxiccmu_softc *sc, uint32_t idx)
{
	uint32_t parent;
	uint32_t reg, div;

	switch (idx) {
	case A64_CLK_LOSC:
		return clock_get_frequency(sc->sc_node, "losc");
	case A64_CLK_HOSC:
		return clock_get_frequency(sc->sc_node, "hosc");
	case A64_CLK_PLL_PERIPH0:
		/* Not hardcoded, but recommended. */
		return 600000000;
	case A64_CLK_PLL_PERIPH0_2X:
		return sxiccmu_a64_get_frequency(sc, A64_CLK_PLL_PERIPH0) * 2;
	case A64_CLK_APB2:
		/* XXX Controlled by a MUX. */
		return 24000000;
	case A64_CLK_AHB1:
		reg = SXIREAD4(sc, CCU_AHB1_APB1_CFG_REG);
		div = CCU_AHB1_CLK_DIV_RATIO(reg);
		switch (reg & CCU_AHB1_CLK_SRC_SEL) {
		case CCU_AHB1_CLK_SRC_SEL_LOSC:
			parent = A64_CLK_LOSC;
			break;
		case CCU_AHB1_CLK_SRC_SEL_OSC24M:
			parent = A64_CLK_HOSC;
			break;
		case CCU_AHB1_CLK_SRC_SEL_AXI:
			parent = A64_CLK_AXI;
			break;
		case CCU_AHB1_CLK_SRC_SEL_PERIPH0:
			parent = A64_CLK_PLL_PERIPH0;
			div *= CCU_AHB1_PRE_DIV(reg);
			break;
		default:
			return 0;
		}
		return sxiccmu_ccu_get_frequency(sc, &parent) / div;
	case A64_CLK_AHB2:
		reg = SXIREAD4(sc, CCU_AHB2_CFG_REG);
		switch (reg & CCU_AHB2_CLK_CFG) {
		case 0:
			parent = A64_CLK_AHB1;
			div = 1;
			break;
		case 1:
			parent = A64_CLK_PLL_PERIPH0;
			div = 2;
			break;
		default:
			return 0;
		}
		return sxiccmu_ccu_get_frequency(sc, &parent) / div;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

uint32_t
sxiccmu_a80_get_frequency(struct sxiccmu_softc *sc, uint32_t idx)
{
	switch (idx) {
	case A80_CLK_PLL_PERIPH0:
		/* Not hardcoded, but recommended. */
		return 960000000;
	case A80_CLK_APB1:
		/* XXX Controlled by a MUX. */
		return 24000000;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

uint32_t
sxiccmu_h3_get_frequency(struct sxiccmu_softc *sc, uint32_t idx)
{
	uint32_t parent;
	uint32_t reg, div;

	switch (idx) {
	case H3_CLK_LOSC:
		return clock_get_frequency(sc->sc_node, "losc");
	case H3_CLK_HOSC:
		return clock_get_frequency(sc->sc_node, "hosc");
	case H3_CLK_PLL_PERIPH0:
		/* Not hardcoded, but recommended. */
		return 600000000;
	case H3_CLK_APB2:
		/* XXX Controlled by a MUX. */
		return 24000000;
	case H3_CLK_AHB1:
		reg = SXIREAD4(sc, CCU_AHB1_APB1_CFG_REG);
		div = CCU_AHB1_CLK_DIV_RATIO(reg);
		switch (reg & CCU_AHB1_CLK_SRC_SEL) {
		case CCU_AHB1_CLK_SRC_SEL_LOSC:
			parent = H3_CLK_LOSC;
			break;
		case CCU_AHB1_CLK_SRC_SEL_OSC24M:
			parent = H3_CLK_HOSC;
			break;
		case CCU_AHB1_CLK_SRC_SEL_AXI:
			parent = H3_CLK_AXI;
			break;
		case CCU_AHB1_CLK_SRC_SEL_PERIPH0:
			parent = H3_CLK_PLL_PERIPH0;
			div *= CCU_AHB1_PRE_DIV(reg);
			break;
		default:
			return 0;
		}
		return sxiccmu_ccu_get_frequency(sc, &parent) / div;
	case H3_CLK_AHB2:
		reg = SXIREAD4(sc, CCU_AHB2_CFG_REG);
		switch (reg & CCU_AHB2_CLK_CFG) {
		case 0:
			parent = H3_CLK_AHB1;
			div = 1;
			break;
		case 1:
			parent = H3_CLK_PLL_PERIPH0;
			div = 2;
			break;
		default:
			return 0;
		}
		return sxiccmu_ccu_get_frequency(sc, &parent) / div;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

uint32_t
sxiccmu_nop_get_frequency(struct sxiccmu_softc *sc, uint32_t idx)
{
	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
sxiccmu_ccu_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct sxiccmu_softc *sc = cookie;
	uint32_t idx = cells[0];

	return sc->sc_set_frequency(sc, idx, freq);
}

int
sxiccmu_a10_set_frequency(struct sxiccmu_softc *sc, uint32_t idx, uint32_t freq)
{
	struct sxiccmu_clock clock;
	uint32_t parent, parent_freq;

	switch (idx) {
	case A10_CLK_MMC0:
	case A10_CLK_MMC1:
	case A10_CLK_MMC2:
	case A10_CLK_MMC3:
		clock.sc_iot = sc->sc_iot;
		bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    sc->sc_gates[idx].reg, 4, &clock.sc_ioh);
		parent = A10_CLK_PLL_PERIPH;
		parent_freq = sxiccmu_ccu_get_frequency(sc, &parent);
		return sxiccmu_mmc_do_set_frequency(&clock, freq, parent_freq);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

int
sxiccmu_a64_set_frequency(struct sxiccmu_softc *sc, uint32_t idx, uint32_t freq)
{
	struct sxiccmu_clock clock;
	uint32_t parent, parent_freq;

	switch (idx) {
	case A64_CLK_MMC0:
	case A64_CLK_MMC1:
	case A64_CLK_MMC2:
		clock.sc_iot = sc->sc_iot;
		bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    sc->sc_gates[idx].reg, 4, &clock.sc_ioh);
		parent = A64_CLK_PLL_PERIPH0_2X;
		parent_freq = sxiccmu_ccu_get_frequency(sc, &parent);
		return sxiccmu_mmc_do_set_frequency(&clock, freq, parent_freq);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

int
sxiccmu_a80_set_frequency(struct sxiccmu_softc *sc, uint32_t idx, uint32_t freq)
{
	struct sxiccmu_clock clock;
	uint32_t parent, parent_freq;

	switch (idx) {
	case A80_CLK_MMC0:
	case A80_CLK_MMC1:
	case A80_CLK_MMC2:
		clock.sc_iot = sc->sc_iot;
		bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    sc->sc_gates[idx].reg, 4, &clock.sc_ioh);
		parent = A80_CLK_PLL_PERIPH0;
		parent_freq = sxiccmu_ccu_get_frequency(sc, &parent);
		return sxiccmu_mmc_do_set_frequency(&clock, freq, parent_freq);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

int
sxiccmu_h3_set_frequency(struct sxiccmu_softc *sc, uint32_t idx, uint32_t freq)
{
	struct sxiccmu_clock clock;
	uint32_t parent, parent_freq;

	switch (idx) {
	case H3_CLK_MMC0:
	case H3_CLK_MMC1:
	case H3_CLK_MMC2:
		clock.sc_iot = sc->sc_iot;
		bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    sc->sc_gates[idx].reg, 4, &clock.sc_ioh);
		parent = H3_CLK_PLL_PERIPH0;
		parent_freq = sxiccmu_ccu_get_frequency(sc, &parent);
		return sxiccmu_mmc_do_set_frequency(&clock, freq, parent_freq);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

int
sxiccmu_nop_set_frequency(struct sxiccmu_softc *sc, uint32_t idx, uint32_t freq)
{
	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

void
sxiccmu_ccu_enable(void *cookie, uint32_t *cells, int on)
{
	struct sxiccmu_softc *sc = cookie;
	uint32_t idx = cells[0];
	int reg, bit;

	clock_enable_all(sc->sc_node);

	if (idx >= sc->sc_ngates ||
	    (sc->sc_gates[idx].reg == 0 && sc->sc_gates[idx].bit == 0)) {
		printf("%s: 0x%08x\n", __func__, cells[0]);
		return;
	}

	reg = sc->sc_gates[idx].reg;
	bit = sc->sc_gates[idx].bit;

	if (on)
		SXISET4(sc, reg, (1U << bit));
	else
		SXICLR4(sc, reg, (1U << bit));
}

void
sxiccmu_ccu_reset(void *cookie, uint32_t *cells, int assert)
{
	struct sxiccmu_softc *sc = cookie;
	uint32_t idx = cells[0];
	int reg, bit;

	reset_deassert_all(sc->sc_node);

	if (idx >= sc->sc_nresets || 
	    (sc->sc_resets[idx].reg == 0 && sc->sc_gates[idx].bit == 0)) {
		printf("%s: 0x%08x\n", __func__, cells[0]);
		return;
	}

	reg = sc->sc_resets[idx].reg;
	bit = sc->sc_resets[idx].bit;
	
	if (assert)
		SXICLR4(sc, reg, (1U << bit));
	else
		SXISET4(sc, reg, (1U << bit));
}
