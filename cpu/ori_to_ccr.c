#include "common.h"
#include "cpu.h"
#include "cprint.h"
#include "mmu.h"

static void ori_to_ccr(struct cpu *cpu, WORD op)
{
  WORD d;

  ENTER;

  ADD_CYCLE(20);
  d = bus_read_word(cpu->pc)&0x1f;
  cpu->pc += 2;
  cpu_set_sr(cpu->sr|d);
  cpu_prefetch();
}

static struct cprint *ori_to_ccr_print(LONG addr, WORD op)
{
  struct cprint *ret;

  ret = cprint_alloc(addr);

  strcpy(ret->instr, "ORI");
  sprintf(ret->data, "#$%x,CCR", bus_read_word_print(addr+ret->size)&0xff);
  ret->size += 2;
  
  return ret;
}

void ori_to_ccr_init(void *instr[], void *print[])
{
  instr[0x003c] = ori_to_ccr;
  print[0x003c] = ori_to_ccr_print;
}



