#include "common.h"
#include "cpu.h"
#include "cprint.h"
#include "mmu.h"
#include "ea.h"

static void subi_b(struct cpu *cpu, WORD op)
{
  BYTE s,d,r;
  
  s = bus_read_word(cpu->pc)&0xff;
  cpu->pc += 2;
  if(op&0x38) {
    ADD_CYCLE(12);
  } else {
    ADD_CYCLE(8);
  }
  d = ea_read_byte(cpu, op&0x3f, 1);
  r = d-s;
  ea_set_prefetch_before_write();
  ea_write_byte(cpu, op&0x3f, r);
  cpu_set_flags_sub(cpu, s&0x80, d&0x80, r&0x80, r);
}

static void subi_w(struct cpu *cpu, WORD op)
{
  WORD s,d,r;
  
  s = bus_read_word(cpu->pc);
  cpu->pc += 2;
  if(op&0x38) {
    ADD_CYCLE(12);
  } else {
    ADD_CYCLE(8);
  }
  d = ea_read_word(cpu, op&0x3f, 1);
  r = d-s;
  ea_set_prefetch_before_write();
  ea_write_word(cpu, op&0x3f, r);
  cpu_set_flags_sub(cpu, s&0x8000, d&0x8000, r&0x8000, r);
}

static void subi_l(struct cpu *cpu, WORD op)
{
  LONG s,d,r;
  
  s = bus_read_long(cpu->pc);
  cpu->pc += 4;
  if(op&0x38) {
    ADD_CYCLE(20);
  } else {
    ADD_CYCLE(16);
  }
  d = ea_read_long(cpu, op&0x3f, 1);
  r = d-s;
  ea_set_prefetch_before_write();
  ea_write_long(cpu, op&0x3f, r);
  cpu_set_flags_sub(cpu, s&0x80000000, d&0x80000000, r&0x80000000, r);
}

static void subi(struct cpu *cpu, WORD op)
{
  ENTER;

  switch((op&0xc0)>>6) {
  case 0:
    subi_b(cpu, op);
    break;
  case 1:
    subi_w(cpu, op);
    break;
  case 2:
    subi_l(cpu, op);
    break;
  }
  if(!cpu->has_prefetched)
    cpu_prefetch();
}

static struct cprint *subi_print(LONG addr, WORD op)
{
  int s;
  struct cprint *ret;

  ret = cprint_alloc(addr);

  s = (op&0xc0)>>6;

  switch(s) {
  case 0:
    strcpy(ret->instr, "SUBI.B");
    sprintf(ret->data, "#$%x,", bus_read_word_print(addr+ret->size)&0xff);
    ret->size += 2;
    break;
  case 1:
    strcpy(ret->instr, "SUBI.W");
    sprintf(ret->data, "#$%x,", bus_read_word_print(addr+ret->size));
    ret->size += 2;
    break;
  case 2:
    strcpy(ret->instr, "SUBI.L");
    sprintf(ret->data, "#$%x,", bus_read_long_print(addr+ret->size));
    ret->size += 4;
    break;
  }
  ea_print(ret, op&0x3f, s);

  return ret;
}

void subi_init(void *instr[], void *print[])
{
  int i;

  for(i=0;i<0xc0;i++) {
    if(ea_valid(i&0x3f, EA_INVALID_DST|EA_INVALID_A)) {
      instr[0x0400|i] = subi;
      print[0x0400|i] = subi_print;
    }
  }
}

