/*
 * Custom MMU	Connected to
 * pin 51	GLUE:VSYNC
 * ?		SHIFTER:LOAD
 */

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "cpu.h"
#include "mfp.h"
#include "shifter.h"
#include "ikbd.h"
#include "acia.h"
#include "psg.h"
#include "dma.h"
#include "fdc.h"
#include "mmu.h"
#include "glue.h"
#include "ram.h"
#include "state.h"
#include "diag.h"

#define MMUSIZE 64
#define MMUBASE 0xff8200

/* Used to prevent extra cycle counts when reading from memory for the only purpose of printing the value */
int mmu_print_state = 0;

static uint8_t mmu_module_at_addr[1<<24];
static uint8_t mmu_module_count = 0;
static struct mmu *mmu_module_by_id[256];
static uint8_t bus_error_id;
static uint8_t bus_error_odd_addr_id;
static LONG scraddr = 0;
static LONG scrptr = 0;
static BYTE syncreg;

#define MEM_READ(size, addr) mmu_module_by_id[mmu_module_at_addr[addr&0xffffff]]->read_##size(addr&0xffffff)
#define MEM_WRITE(size, addr, data) mmu_module_by_id[mmu_module_at_addr[addr&0xffffff]]->write_##size(addr&0xffffff, data)

HANDLE_DIAGNOSTICS(mmu)

static struct mmu *find_module_by_id(char *id)
{
  struct mmu *module;
  int i;

  for(i=0;i<mmu_module_count;i++) {
    module = mmu_module_by_id[i];
    if(!strncmp(id, module->id, 4)) {
      return module;
    }
  }

  return NULL;
}

void mmu_send_bus_error(int reading, LONG addr)
{
  int flags = 0;

  if(reading) {
    flags |= CPU_BUSERR_READ;
  } else {
    flags |= CPU_BUSERR_WRITE;
  }
  if(cpu->pc == addr) {
    flags |= CPU_BUSERR_INSTR;
  } else {
    flags |= CPU_BUSERR_DATA;
  }
  
  DEBUG("BUS ERROR at 0x%08x", addr);
  if(mmu_device->verbosity > 4)
    cpu_print_status(CPU_USE_LAST_PC);

  cpu_set_bus_error(flags, addr);
}

void mmu_send_address_error(int reading, LONG addr)
{
  int flags = 0;

  if(reading) {
    flags |= CPU_ADDRERR_READ;
  } else {
    flags |= CPU_ADDRERR_WRITE;
  }
  if(cpu->pc == addr) {
    flags |= CPU_ADDRERR_INSTR;
  } else {
    flags |= CPU_ADDRERR_DATA;
  }
  
  DEBUG("ADDRESS ERROR at 0x%08x", addr);
  if(mmu_device->verbosity > 4)
    cpu_print_status(CPU_USE_LAST_PC);
  
  cpu_set_address_error(flags, addr);
}

static BYTE mmu_read_byte_bus_error(LONG addr)
{
  mmu_send_bus_error(CPU_BUSERR_READ, addr);
  return 0;
}

static WORD mmu_read_word_bus_error(LONG addr)
{
  mmu_send_bus_error(CPU_BUSERR_READ, addr);
  return 0;
}

static void mmu_write_byte_bus_error(LONG addr, BYTE data)
{
  mmu_send_bus_error(CPU_BUSERR_WRITE, addr);
}

static void mmu_write_word_bus_error(LONG addr, WORD data)
{
  mmu_send_bus_error(CPU_BUSERR_WRITE, addr);
}

static WORD mmu_read_word_address_error(LONG addr)
{
  mmu_send_address_error(CPU_ADDRERR_READ, addr);
  return 0;
}

static void mmu_write_word_address_error(LONG addr, WORD data)
{
  mmu_send_address_error(CPU_ADDRERR_WRITE, addr);
}

static struct mmu *mmu_bus_error_module()
{
  struct mmu *bus_error;

  bus_error = mmu_create("BSER", "Bus error");

  bus_error->read_byte = mmu_read_byte_bus_error;
  bus_error->read_word = mmu_read_word_bus_error;
  bus_error->write_byte = mmu_write_byte_bus_error;
  bus_error->write_word = mmu_write_word_bus_error;

  return bus_error;
}

static struct mmu *mmu_clone_module(struct mmu *module)
{
  struct mmu *clone;
  clone = mmu_create(module->id, module->name);

  clone->read_byte = module->read_byte;
  clone->read_word =  module->read_word;
  clone->write_byte = module->write_byte;
  clone->write_word = module->write_word;

  return clone;
}

static struct mmu *mmu_clone_module_for_address_error(struct mmu *module)
{
  struct mmu *address_error_clone;
  address_error_clone= mmu_clone_module(module);
  address_error_clone->read_word = mmu_read_word_address_error;
  address_error_clone->write_word = mmu_write_word_address_error;

  return address_error_clone;
}

static uint8_t mmu_get_module_id(struct mmu *module)
{
  uint8_t id;
  id = mmu_module_count;
  mmu_module_by_id[id] = module;
  mmu_module_count++;
  return id;
}

/* Make sure there are targets for all read/write functions
 * making them return bus error in case they don't have a
 * better target already
 */
static void mmu_populate_functions(struct mmu *module)
{
  if(!module->read_byte) {
    module->read_byte = mmu_read_byte_bus_error;
  }
  if(!module->read_word) {
    module->read_word = mmu_read_word_bus_error;
  }
  if(!module->write_byte) {
    module->write_byte = mmu_write_byte_bus_error;
  }
  if(!module->write_word) {
    module->write_word = mmu_write_word_bus_error;
  }
}

static BYTE mmu_read_byte(LONG addr)
{
  switch(addr) {
  case 0xff8201:
    return (scraddr&0xff0000)>>16;
  case 0xff8203:
    return (scraddr&0xff00)>>8;
  case 0xff8205:
    return (scrptr&0xff0000)>>16;
  case 0xff8207:
    return (scrptr&0xff00)>>8;
  case 0xff8209:
    return scrptr&0xff;
  case 0xff820a:
    return syncreg;
  default:
    return 0;
  }
}

static WORD mmu_read_word(LONG addr)
{
  return (mmu_read_byte(addr)<<8)|mmu_read_byte(addr+1);
}

static void mmu_write_byte(LONG addr, BYTE data)
{
  switch(addr) {
  case 0xff8201:
    scraddr = (scraddr&0xff00)|(data<<16);
    DEBUG("Screen address: %06x", scraddr);
    return;
  case 0xff8203:
    scraddr = (scraddr&0xff0000)|(data<<8);
    DEBUG("Screen address: %06x", scraddr);
    return;
  case 0xff820a:
    syncreg = data;
    DEBUG("Sync register: %02x", data);
    glue_set_sync(data & 2);
    return;
  }
}

static void mmu_write_word(LONG addr, WORD data)
{
  mmu_write_byte(addr, (data&0xff00)>>8);
  mmu_write_byte(addr+1, (data&0xff));
}

void mmu_init()
{
  int i;
  struct mmu *mmu;
  struct mmu *bus_error_module;
  struct mmu *bus_error_module_odd_addr;

  bus_error_module = mmu_bus_error_module();
  bus_error_id = mmu_get_module_id(bus_error_module);

  bus_error_module_odd_addr = mmu_clone_module_for_address_error(bus_error_module);
  bus_error_odd_addr_id = mmu_get_module_id(bus_error_module_odd_addr);
  
  /* Populate entire memory space with default module that gives bus error or address error */
  for(i=0;i<16777216;i++) {
    if(i&1) {
      /* Odd addresses will give address errors on WORD/LONG */
      mmu_module_at_addr[i] = bus_error_odd_addr_id;
    } else {
      mmu_module_at_addr[i] = bus_error_id;
    }
  }

  mmu = mmu_create("MMU0", "Memory Controller");
  mmu->start = MMUBASE;
  mmu->size = MMUSIZE;
  mmu->read_byte = mmu_read_byte;
  mmu->read_word = mmu_read_word;
  mmu->write_byte = mmu_write_byte;
  mmu->write_word = mmu_write_word;
  mmu->diagnostics = mmu_diagnostics;
  mmu_register(mmu);
}



/* Print functions are used mainly for displaying the result in the debugger, or to STDOUT
 * This means that it should be read without causing bus/address errors, regardless of size
 * and location
 */

BYTE bus_read_byte_print(LONG addr)
{
  BYTE value;
  addr &= 0xffffff;
  if(mmu_module_at_addr[addr] == bus_error_id || mmu_module_at_addr[addr] == bus_error_odd_addr_id) {
    return 0;
  }
  mmu_print_state = 1;
  value = MEM_READ(byte, addr);
  mmu_print_state = 0;
  return value;
}

WORD bus_read_word_print(LONG addr)
{
  BYTE low,high;
  addr &= 0xffffff;

  high = bus_read_byte_print(addr);
  low = bus_read_byte_print(addr+1);

  return (high<<8)|low;
}

LONG bus_read_long_print(LONG addr)
{
  WORD low,high;
  addr &= 0xffffff;

  high = bus_read_word_print(addr);
  low = bus_read_word_print(addr+2);

  return (high<<16)|low;
}



/* These are the normal read/write functions that should cause bus/address errors
 * if appropriate
 */

BYTE bus_read_byte(LONG addr)
{
  return MEM_READ(byte, addr);
}

WORD bus_read_word(LONG addr)
{
  return MEM_READ(word, addr);
}

LONG bus_read_long(LONG addr)
{
  return (bus_read_word(addr)<<16) + bus_read_word(addr+2);
}

void bus_write_byte(LONG addr, BYTE data)
{
  MEM_WRITE(byte, addr, data);
}

void bus_write_word(LONG addr, WORD data)
{
  MEM_WRITE(word, addr, data);
}

void bus_write_long(LONG addr, LONG data)
{
  bus_write_word(addr, data >> 16);
  bus_write_word(addr+2, data & 0xffff);
}




struct mmu_state *mmu_state_collect()
{
  int i;
  struct mmu *module;
  struct mmu_state *new,*top;

  top = NULL;

  for(i=0; i<mmu_module_count; i++) {
    module = mmu_module_by_id[i];
    if(module->state_collect != NULL) {
      new = xmalloc(sizeof(struct mmu_state));
      if(new != NULL) {
	strncpy(new->id, module->id, 4);
	if(module->state_collect(new) == STATE_VALID) {
	  new->next = top;
	  top = new;
	} else {
	  free(new);
	}
      }
    }
  }
  
  return top;
}

void mmu_state_restore(struct mmu_state *state)
{
  struct mmu *module;
  struct mmu_state *t;

  t = state;
  
  while(t) {
    module = find_module_by_id(t->id);
    if(module != NULL && module->state_restore != NULL) {
      module->state_restore(t);
    }
    t = t->next;
  }
}

struct mmu * mmu_create(const char *id, const char *name)
{
  struct mmu *device = xmalloc(sizeof(struct mmu));
  if(device == NULL)
    FATAL("Could not allocate device");

  memset(device, 0, sizeof(struct mmu));
  memcpy(device->id, id, sizeof device->id);
  device->name = name;

  return device;
}

void mmu_register(struct mmu *data)
{
  extern void diagnostics_level(struct mmu *, int);
  extern int verbosity;
  struct mmu *data_address_error;
  uint8_t data_id, data_address_error_id;
  int i,addr;

  if(!data) return;
  
  data_id = mmu_get_module_id(data);
  mmu_populate_functions(data);
  data_address_error = mmu_clone_module_for_address_error(data);
  data_address_error_id = mmu_get_module_id(data_address_error);

  for(i=0;i<data->size;i++) {
    addr = data->start + i;
    if(addr&1) {
      mmu_module_at_addr[addr] = data_address_error_id;
    } else {
      mmu_module_at_addr[addr] = data_id;
    }
  }

  diagnostics_level(data, verbosity);
}

void mmu_print_map()
{
  int i;
  struct mmu *module;

  for(i=0;i<mmu_module_count;i++) {
    module = mmu_module_by_id[i];
    printf("Name : %s\n", module->name);
    printf("Start: 0x%08x\n", module->start);
    printf("End  : 0x%08x\n", module->start+module->size-1);
    printf("Size : %d\n", module->size);
    printf("\n");
  }
}

void mmu_do_interrupts(struct cpu *cpu)
{
  int i;
  struct mmu *module;

  for(i=0;i<mmu_module_count;i++) {
    module = mmu_module_by_id[i];
    if(module->interrupt)
      module->interrupt(cpu);
  }

  fdc_do_interrupts(cpu);
  ikbd_do_interrupt(cpu);
}

void mmu_de(int enable)
{
  if(enable) {
    TRACE("Display enable");
    shifter_load(ram_read_word(scrptr));
    scrptr += 2;
  } else {
    shifter_border();
  }
}

void mmu_vsync(void)
{
  DEBUG("Vsync");
  scrptr = scraddr;
}
