#include <amtest.h>

#define READ_WORD(addr)        (*((volatile uint32_t *)(addr)))
#define WRITE_WORD(addr, data) (*((volatile uint32_t *)(addr)) = (data))
#define EXTRACT_BIT(data, i)   ((data) & (0x1UL << (i)))
#define SET_BIT(data, i)       ((data) | (0x1UL << (i)))
#define CLEAR_BIT(data, i)     ((data) ^ EXTRACT_BIT(data, i))

#define INTR_GEN_ADDR          (0x40070000UL)
#define INTR_REG_WIDTH         32
#define INTR_REG_NUM           8
#define INTR_REG_ADDR(i)       ((INTR_GEN_ADDR) + ((i) << 2))
#define INTR_REG_INDEX(i)      INTR_REG_ADDR(((i) / INTR_REG_WIDTH))
#define INTR_REG_OFFSET(i)     ((i) % INTR_REG_WIDTH)

#define READ_INTR_REG(i)  READ_WORD(INTR_REG_ADDR(i))
#define READ_INTR(i)     EXTRACT_BIT(READ_INTR_REG(INTR_REG_INDEX(i)), INTR_REG_OFFSET(i))
#define CLEAR_INTR(i)    WRITE_WORD(INTR_REG_INDEX(i), CLEAR_BIT(READ_INTR_REG(INTR_REG_INDEX(i)), INTR_REG_OFFSET(i)))
#define SET_INTR(i)      WRITE_WORD(INTR_REG_INDEX(i), SET_BIT(READ_INTR_REG(INTR_REG_INDEX(i)), INTR_REG_OFFSET(i)))

#define PLIC_BASE_ADDR         (0x3c000000UL)
#define PLIC_PENDING           (PLIC_BASE_ADDR + 0x1000UL)
#define PLIC_ENABLE            (PLIC_BASE_ADDR + 0x2000UL)
#define PLIC_CLAIM             (PLIC_BASE_ADDR + 0x200004UL)
#define PLIC_EXT_INTR_OFFSET   2

#define MAX_EXT_INTR 150

static volatile uint32_t should_claim = -1;

void do_ext_intr() {
  uint32_t claim = READ_WORD(PLIC_CLAIM);
  printf("ext_intr: claim %d should_claim %d\n", claim, should_claim);
  if (claim) {
    assert(claim == should_claim);
    CLEAR_INTR(claim - 1);
    WRITE_WORD(PLIC_CLAIM, claim);
    should_claim = -1;
    // simply write to mie to trigger an illegal instruction exception
    // m mode will set mie to enable meip
    asm volatile("csrs mie, 0");
  }
  else {
    read_key();
  }
}

_Context *external_trap(_Event ev, _Context *ctx) {
  switch(ev.event) {
    case _EVENT_IRQ_TIMER:
      printf("t"); break;
    case _EVENT_IRQ_IODEV:
      printf("d"); do_ext_intr(); break;
    case _EVENT_YIELD:
      printf("y"); break;
    default:
      printf("u"); _halt(1);
  }
  return ctx;
}

static void plic_intr_init()
{
  // TODO: fix init plic priority logic
  for (int i = 1; i <= MAX_EXT_INTR; i++)
    WRITE_WORD(PLIC_BASE_ADDR + i*sizeof(uint32_t), 0xffffffff);
}

void external_intr() {
  // enable supervisor external interrupts
  asm volatile("csrs sie, %0" : : "r"((1 << 9)));
  asm volatile("csrs sstatus, 2");
  plic_intr_init();
  // trigger interrupts
  const uint32_t MAX_RAND_ITER = 500;
  int r = 0;
  for (int i = 0; i < MAX_RAND_ITER; i++) {
    // int r = rand();
    should_claim = (r % MAX_EXT_INTR) + PLIC_EXT_INTR_OFFSET;
    printf("should_claim %d, setting intr\n", should_claim);
    WRITE_WORD(PLIC_ENABLE + (should_claim / 32) * 4, (1UL << (should_claim % 32)));
    SET_INTR(should_claim - PLIC_EXT_INTR_OFFSET);
    int counter = 0;
    while (should_claim != -1 && counter < 100) {
      counter++;
    }
    if (should_claim != -1) {
      printf("external interrupt is not triggered!\n");
      _halt(1);
    }
    r++;
  }
  printf("external interrupt test passed!!!\n");
}

