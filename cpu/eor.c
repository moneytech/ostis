#include "common.h"
#include "cpu.h"
#include "cprint.h"
#include "ea.h"

static void eor_b(struct cpu *cpu, WORD op)
{
  BYTE r;
  int reg;

  reg = (op&0xe00)>>9;

  if(op&0x100) {
    ADD_CYCLE(8);
    r = cpu->d[reg] ^ ea_read_byte(cpu, op&0x3f, 1);
    ea_set_prefetch_before_write();
    ea_write_byte(cpu, op&0x3f, r);
  } else {
    ADD_CYCLE(4);
    r = cpu->d[reg] ^ ea_read_byte(cpu, op&0x3f, 0);
    cpu->d[reg] = (cpu->d[reg]&0xffffff00)|r;
  }
  cpu_set_flags_move(cpu, r&0x80, r);
}

static void eor_w(struct cpu *cpu, WORD op)
{
  WORD r;
  int reg;

  reg = (op&0xe00)>>9;

  if(op&0x100) {
    ADD_CYCLE(8);
    r = cpu->d[reg] ^ ea_read_word(cpu, op&0x3f, 1);
    ea_set_prefetch_before_write();
    ea_write_word(cpu, op&0x3f, r);
  } else {
    ADD_CYCLE(4);
    r = cpu->d[reg] ^ ea_read_word(cpu, op&0x3f, 0);
    cpu->d[reg] = (cpu->d[reg]&0xffff0000)|r;
  }
  cpu_set_flags_move(cpu, r&0x8000, r);
}

static void eor_l(struct cpu *cpu, WORD op)
{
  LONG r;
  int reg;

  reg = (op&0xe00)>>9;

  if(op&0x100) {
    ADD_CYCLE(12);
    r = cpu->d[reg] ^ ea_read_long(cpu, op&0x3f, 1);
    ea_set_prefetch_before_write();
    ea_write_long(cpu, op&0x3f, r);
  } else {
    ADD_CYCLE(6);
    r = cpu->d[reg] ^ ea_read_long(cpu, op&0x3f, 0);
    cpu->d[reg] = r;
  }
  cpu_set_flags_move(cpu, r&0x80000000, r);
}

static void eor(struct cpu *cpu, WORD op)
{
  ENTER;

  switch((op&0xc0)>>6) {
  case 0:
    eor_b(cpu, op);
    break;
  case 1:
    eor_w(cpu, op);
    break;
  case 2:
    eor_l(cpu, op);
    break;
  }
  if(!cpu->has_prefetched)
    cpu_prefetch();
}

static struct cprint *eor_print(LONG addr, WORD op)
{
  int s,r;
  struct cprint *ret;

  ret = cprint_alloc(addr);

  s = (op&0xc0)>>6;
  r = (op&0xe00)>>9;
  
  switch(s) {
  case 0:
    strcpy(ret->instr, "EOR.B");
    break;
  case 1:
    strcpy(ret->instr, "EOR.W");
    break;
  case 2:
    strcpy(ret->instr, "EOR.L");
    break;
  }

  if(op&0x100) {
    sprintf(ret->data, "D%d,", r);
    ea_print(ret, op&0x3f, s);
  } else {
    ea_print(ret, op&0x3f, s);
    sprintf(ret->data, "%s,D%d", ret->data, r);
  }
  
  return ret;
}

void eor_init(void *instr[], void *print[])
{
  int i,r;

  for(r=0;r<8;r++) {
    for(i=0;i<0x40;i++) {
      if(ea_valid(i, EA_INVALID_DST|EA_INVALID_A)) {
	instr[0xb000|(r<<9)|i] = eor;
	instr[0xb040|(r<<9)|i] = eor;
	instr[0xb080|(r<<9)|i] = eor;
	instr[0xb100|(r<<9)|i] = eor;
	instr[0xb140|(r<<9)|i] = eor;
	instr[0xb180|(r<<9)|i] = eor;
	print[0xb000|(r<<9)|i] = eor_print;
	print[0xb040|(r<<9)|i] = eor_print;
	print[0xb080|(r<<9)|i] = eor_print;
	print[0xb100|(r<<9)|i] = eor_print;
	print[0xb140|(r<<9)|i] = eor_print;
	print[0xb180|(r<<9)|i] = eor_print;
      }
    }
  }
}
