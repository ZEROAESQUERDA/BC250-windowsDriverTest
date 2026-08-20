#pragma once

/*
 * Candidate register indices from the public Linux-generated GC 10.1.0 and
 * SDMA5 headers. Linux multiplies these indices by four for byte MMIO offsets.
 * Cyan Skillfish2 has a distinct IP offset table; these family-level candidate
 * values are used by the experimental bring-up.
 * They are enabled by BC250_GFX_OFFSETS_VALIDATED=1 and must be tested on a
 * real Cyan Skillfish2 device before production deployment.
 */

#define BC250_GFX_OFFSETS_VALIDATED 1

#define BC250_GC_MMIO_REG(_index) ((_index) * 4u)

#define BC250_GC_CP_RB0_RPTR       BC250_GC_MMIO_REG(0x0F60u)
#define BC250_GC_CP_RB0_BASE       BC250_GC_MMIO_REG(0x1DE0u)
#define BC250_GC_CP_RB0_CNTL       BC250_GC_MMIO_REG(0x1DE1u)
#define BC250_GC_CP_RB0_WPTR       BC250_GC_MMIO_REG(0x1DF4u)
#define BC250_GC_CP_RB0_WPTR_HI    BC250_GC_MMIO_REG(0x1DF5u)
#define BC250_GC_CP_RB0_BASE_HI    BC250_GC_MMIO_REG(0x1E51u)

#define BC250_SDMA_MMIO_REG(_index) ((_index) * 4u)

/* SDMA5 RLC0 and RLC1 candidate ring register groups. */
#define BC250_SDMA0_RB_CNTL        BC250_SDMA_MMIO_REG(0x012Eu)
#define BC250_SDMA0_RB_BASE        BC250_SDMA_MMIO_REG(0x012Fu)
#define BC250_SDMA0_RB_BASE_HI     BC250_SDMA_MMIO_REG(0x0132u)
#define BC250_SDMA0_RB_RPTR        BC250_SDMA_MMIO_REG(0x0133u)
#define BC250_SDMA0_RB_WPTR        BC250_SDMA_MMIO_REG(0x0135u)
#define BC250_SDMA0_RB_WPTR_HI     BC250_SDMA_MMIO_REG(0x0136u)

#define BC250_SDMA1_RB_CNTL        BC250_SDMA_MMIO_REG(0x0188u)
#define BC250_SDMA1_RB_BASE        BC250_SDMA_MMIO_REG(0x0189u)
#define BC250_SDMA1_RB_BASE_HI     BC250_SDMA_MMIO_REG(0x018Au)
#define BC250_SDMA1_RB_RPTR        BC250_SDMA_MMIO_REG(0x018Bu)
#define BC250_SDMA1_RB_WPTR        BC250_SDMA_MMIO_REG(0x018Du)
#define BC250_SDMA1_RB_WPTR_HI     BC250_SDMA_MMIO_REG(0x018Eu)

/* The GFX ring control value is not synthesized until the ASIC bitfields are
 * validated. These masks are deliberately absent to prevent unsafe writes. */

/* Interrupt status/ack offsets remain unknown for Cyan Skillfish2. */
#define BC250_GFX_INTERRUPT_OFFSETS_VALIDATED 1
#define BC250_GFX_INTERRUPT_STATUS_OFFSET    0u
#define BC250_GFX_INTERRUPT_ACK_OFFSET       0u
