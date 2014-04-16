/*
 * Copyright (C) 2014 Ole André Vadla Ravnås <ole.andre.ravnas@tillitech.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* Useful reference: C4.1 A64 instruction index by encoding */

#include "gumarm64relocator.h"

#include "gummemory.h"

#define GUM_MAX_INPUT_INSN_COUNT (100)

typedef struct _GumCodeGenCtx GumCodeGenCtx;

struct _GumCodeGenCtx
{
  const GumArm64Instruction * insn;
  guint32 raw_insn;
  const guint8 * start;
  const guint8 * end;
  guint len;

  GumArm64Writer * output;
};

static gboolean gum_arm64_relocator_rewrite_adr (GumArm64Relocator * self,
    GumCodeGenCtx * ctx);

void
gum_arm64_relocator_init (GumArm64Relocator * relocator,
                          gconstpointer input_code,
                          GumArm64Writer * output)
{
  relocator->input_insns = gum_new (GumArm64Instruction,
      GUM_MAX_INPUT_INSN_COUNT);

  gum_arm64_relocator_reset (relocator, input_code, output);
}

void
gum_arm64_relocator_reset (GumArm64Relocator * relocator,
                           gconstpointer input_code,
                           GumArm64Writer * output)
{
  relocator->input_start = input_code;
  relocator->input_cur = input_code;
  relocator->input_pc = GUM_ADDRESS (input_code);
  relocator->output = output;

  relocator->inpos = 0;
  relocator->outpos = 0;

  relocator->eob = FALSE;
  relocator->eoi = FALSE;
}

void
gum_arm64_relocator_free (GumArm64Relocator * relocator)
{
  gum_free (relocator->input_insns);
}

static guint
gum_arm64_relocator_inpos (GumArm64Relocator * self)
{
  return self->inpos % GUM_MAX_INPUT_INSN_COUNT;
}

static guint
gum_arm64_relocator_outpos (GumArm64Relocator * self)
{
  return self->outpos % GUM_MAX_INPUT_INSN_COUNT;
}

static void
gum_arm64_relocator_increment_inpos (GumArm64Relocator * self)
{
  self->inpos++;
  g_assert_cmpint (self->inpos, >, self->outpos);
}

static void
gum_arm64_relocator_increment_outpos (GumArm64Relocator * self)
{
  self->outpos++;
  g_assert_cmpint (self->outpos, <=, self->inpos);
}

guint
gum_arm64_relocator_read_one (GumArm64Relocator * self,
                              const GumArm64Instruction ** instruction)
{
  guint32 raw_insn;
  GumArm64Instruction * insn;
  guint32 adr_bits;

  if (self->eoi)
    return 0;

  raw_insn = GUINT32_FROM_LE (*((guint32 *) self->input_cur));
  insn = &self->input_insns[gum_arm64_relocator_inpos (self)];

  insn->mnemonic = GUM_ARM64_UNKNOWN;
  insn->address = self->input_cur;
  insn->length = 4;
  insn->pc = self->input_pc;

  adr_bits = raw_insn & 0x9f000000;
  if (adr_bits == 0x10000000)
    insn->mnemonic = GUM_ARM64_ADR;
  else if (adr_bits == 0x90000000)
    insn->mnemonic = GUM_ARM64_ADRP;

  gum_arm64_relocator_increment_inpos (self);

  if (instruction != NULL)
    *instruction = insn;

  self->input_cur += insn->length;
  self->input_pc += insn->length;

  return self->input_cur - self->input_start;
}

GumArm64Instruction *
gum_arm64_relocator_peek_next_write_insn (GumArm64Relocator * self)
{
  if (self->outpos == self->inpos)
    return NULL;

  return &self->input_insns[gum_arm64_relocator_outpos (self)];
}

gpointer
gum_arm64_relocator_peek_next_write_source (GumArm64Relocator * self)
{
  GumArm64Instruction * next;

  next = gum_arm64_relocator_peek_next_write_insn (self);
  if (next == NULL)
    return NULL;

  g_assert_not_reached ();
  return NULL;
}

void
gum_arm64_relocator_skip_one (GumArm64Relocator * self)
{
  GumArm64Instruction * next;

  next = gum_arm64_relocator_peek_next_write_insn (self);
  g_assert (next != NULL);
  gum_arm64_relocator_increment_outpos (self);
}

gboolean
gum_arm64_relocator_write_one (GumArm64Relocator * self)
{
  GumArm64Instruction * cur;
  GumCodeGenCtx ctx;
  gboolean rewritten = FALSE;

  if ((cur = gum_arm64_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;

  if ((ctx.insn = gum_arm64_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;
  gum_arm64_relocator_increment_outpos (self);

  ctx.len = ctx.insn->length;
  ctx.raw_insn = GUINT32_FROM_LE (*((guint32 *) ctx.insn->address));
  ctx.start = ctx.insn->address;
  ctx.end = ctx.start + ctx.len;

  ctx.output = self->output;

  switch (ctx.insn->mnemonic)
  {
    case GUM_ARM64_ADR:
    case GUM_ARM64_ADRP:
      rewritten = gum_arm64_relocator_rewrite_adr (self, &ctx);
      break;
    default:
      break;
  }

  if (!rewritten)
    gum_arm64_writer_put_bytes (ctx.output, ctx.start, ctx.len);

  return TRUE;
}

void
gum_arm64_relocator_write_all (GumArm64Relocator * self)
{
  guint count = 0;

  while (gum_arm64_relocator_write_one (self))
    count++;

  g_assert_cmpuint (count, >, 0);
}

gboolean
gum_arm64_relocator_eob (GumArm64Relocator * self)
{
  return self->eob;
}

gboolean
gum_arm64_relocator_eoi (GumArm64Relocator * self)
{
  return self->eoi;
}

gboolean
gum_arm64_relocator_can_relocate (gpointer address,
                                  guint min_bytes)
{
  guint8 * buf;
  GumArm64Writer cw;
  GumArm64Relocator rl;
  guint reloc_bytes;

  buf = g_alloca (3 * min_bytes);
  gum_arm64_writer_init (&cw, buf);

  gum_arm64_relocator_init (&rl, address, &cw);

  do
  {
    reloc_bytes = gum_arm64_relocator_read_one (&rl, NULL);
    if (reloc_bytes == 0)
      return FALSE;
  }
  while (reloc_bytes < min_bytes);

  gum_arm64_relocator_free (&rl);

  gum_arm64_writer_free (&cw);

  return TRUE;
}

guint
gum_arm64_relocator_relocate (gpointer from,
                              guint min_bytes,
                              gpointer to)
{
  GumArm64Writer cw;
  GumArm64Relocator rl;
  guint reloc_bytes;

  gum_arm64_writer_init (&cw, to);

  gum_arm64_relocator_init (&rl, from, &cw);

  do
  {
    reloc_bytes = gum_arm64_relocator_read_one (&rl, NULL);
    g_assert_cmpuint (reloc_bytes, !=, 0);
  }
  while (reloc_bytes < min_bytes);

  gum_arm64_relocator_write_all (&rl);

  gum_arm64_relocator_free (&rl);
  gum_arm64_writer_free (&cw);

  return reloc_bytes;
}

static gboolean
gum_arm64_relocator_rewrite_adr (GumArm64Relocator * self,
                                 GumCodeGenCtx * ctx)
{
  GumArm64Reg reg;
  guint64 imm_hi, imm_lo;
  guint64 negative_mask;
  union
  {
    gint64 i;
    guint64 u;
  } distance;
  GumAddress absolute_target;

  (void) self;

  reg = ctx->raw_insn & 0x1f;
  imm_hi = (ctx->raw_insn >> 5) & 0x7ffff;
  imm_lo = (ctx->raw_insn >> 29) & 3;

  if (ctx->insn->mnemonic == GUM_ARM64_ADR)
  {
    negative_mask = G_GUINT64_CONSTANT (0xffffffffffe00000);

    if ((imm_hi & 0x40000) != 0)
      distance.u = negative_mask | (imm_hi << 2) | imm_lo;
    else
      distance.u = (imm_hi << 2) | imm_lo;
  }
  else if (ctx->insn->mnemonic == GUM_ARM64_ADRP)
  {
    negative_mask = G_GUINT64_CONSTANT (0xfffffffe00000000);

    if ((imm_hi & 0x40000) != 0)
      distance.u = negative_mask | (imm_hi << 14) | (imm_lo << 12);
    else
      distance.u = (imm_hi << 14) | (imm_lo << 12);
  }
  else
  {
    distance.u = 0;

    g_assert_not_reached ();
  }

  absolute_target = ctx->insn->pc + distance.i;

  gum_arm64_writer_put_ldr_reg_address (ctx->output, reg, absolute_target);

  return TRUE;
}