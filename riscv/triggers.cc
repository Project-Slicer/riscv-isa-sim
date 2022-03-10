#include "triggers.h"
#include "processor.h"
#include "debug_defines.h"

#if 0
#  define D(x) x
#else
#  define D(x)
#endif

namespace trigger {

module_t::module_t(unsigned count) : triggers(count) {
  for (unsigned i = 0; i < count; i++) {
    triggers[i] = new mcontrol_t();
  }
}

bool mcontrol_t::simple_match(unsigned xlen, reg_t value)
{
  switch (match) {
    case MATCH_EQUAL:
      return value == tdata2;
    case MATCH_NAPOT:
      {
        reg_t mask = ~((1 << (cto(tdata2)+1)) - 1);
        return (value & mask) == (tdata2 & mask);
      }
    case MATCH_GE:
      return value >= tdata2;
    case MATCH_LT:
      return value < tdata2;
    case MATCH_MASK_LOW:
      {
        reg_t mask = tdata2 >> (xlen/2);
        return (value & mask) == (tdata2 & mask);
      }
    case MATCH_MASK_HIGH:
      {
        reg_t mask = tdata2 >> (xlen/2);
        return ((value >> (xlen/2)) & mask) == (tdata2 & mask);
      }
  }
  assert(0);
}

match_result_t mcontrol_t::memory_access_match(
  processor_t *proc, operation_t operation, reg_t address, reg_t data)
{
  const state_t *state = proc->get_state();
  if ((operation == OPERATION_EXECUTE && !execute) ||
      (operation == OPERATION_STORE && !store) ||
      (operation == OPERATION_LOAD && !load) ||
      (state->prv == PRV_M && !m) ||
      (state->prv == PRV_S && !s) ||
      (state->prv == PRV_U && !u)) {
    return MATCH_NONE;
  }

  reg_t value;
  if (select) {
    value = data;
  } else {
    value = address;
  }

  D(fprintf(stderr, "match value %lx against tdata2 0x%lx\n", value, tdata2));

  // We need this because in 32-bit mode sometimes the PC bits get sign
  // extended.
  unsigned xlen = proc->get_xlen();
  if (xlen == 32) {
    value &= 0xffffffff;
  }

  if (simple_match(xlen, value)) {
    hit = true;
    if (timing)
      return MATCH_FIRE_AFTER;
    else
      return MATCH_FIRE_BEFORE;
  }

  return MATCH_NONE;
}

match_result_t module_t::memory_access_match(
    action_t *action, operation_t operation, reg_t address, reg_t data)
{
  const state_t *state = proc->get_state();
  if (state->debug_mode)
    return MATCH_NONE;

  D(fprintf(stderr, "module_t memory_access_match on %lx\n", address));

  bool chain_inhibit = false;
  for (auto trigger: triggers) {
    if (chain_inhibit) {
      chain_inhibit = trigger->chain();
    } else {
      match_result_t result = trigger->memory_access_match(proc, operation, address, data);
      if (result == MATCH_NONE) {
        chain_inhibit = trigger->chain();
      } else {
        *action = trigger->action;
        return result;
      }
    }
  }
  return MATCH_NONE;
}

reg_t mcontrol_t::tdata1_read(processor_t *proc) const {
  auto xlen = proc->get_xlen();
  reg_t v = 0;
  if (mcontrol6) {
    v = set_field(v, MCONTROL_TYPE(xlen), 6);
    v = set_field(v, CSR_MCONTROL6_SELECT, select);
    v = set_field(v, CSR_MCONTROL6_TIMING, timing);
  } else {
    v = set_field(v, MCONTROL_TYPE(xlen), 2);
    v = set_field(v, MCONTROL_SELECT, select);
    v = set_field(v, MCONTROL_TIMING, timing);
    v = set_field(v, MCONTROL_MASKMAX(xlen), 0x3f);
  }

  // The below fields are the same in mcontrol and mcontrol6.
  v = set_field(v, MCONTROL_DMODE(xlen), dmode);
  v = set_field(v, MCONTROL_ACTION, action);
  v = set_field(v, MCONTROL_CHAIN, chain_bit);
  v = set_field(v, MCONTROL_MATCH, match);
  v = set_field(v, MCONTROL_M, m);
  v = set_field(v, MCONTROL_S, s);
  v = set_field(v, MCONTROL_U, u);
  v = set_field(v, MCONTROL_EXECUTE, execute);
  v = set_field(v, MCONTROL_STORE, store);
  v = set_field(v, MCONTROL_LOAD, load);

  D(fprintf(stderr, "tdata1 read -> 0x%lx\n", v));

  return v;
}

bool mcontrol_t::tdata1_write(processor_t *proc, reg_t val)
{
  if (dmode && !proc->get_state()->debug_mode) {
    return false;
  }
  D(fprintf(stderr, "tdata1 write <- 0x%lx\n", val));
  auto xlen = proc->get_xlen();
  switch (get_field(val, MCONTROL_TYPE(xlen))) {
    case 2:
      mcontrol6 = false;
      select = get_field(val, MCONTROL_SELECT);
      timing = get_field(val, MCONTROL_TIMING);
      break;
    case 0:
      // "it is guaranteed that writing 0 to tdata1 disables the trigger, and
      // leaves it in a state where tdata2 and tdata3 can be written with any
      // value that makes sense for any trigger type supported by this trigger."
    case 6:
      mcontrol6 = true;
      select = get_field(val, CSR_MCONTROL6_SELECT);
      timing = get_field(val, CSR_MCONTROL6_TIMING);
      break;
    default:
      // Register is WARL, don't change anything.
      return true;
  }

  // The below fields are the same in mcontrol and mcontrol6.
  dmode = get_field(val, MCONTROL_DMODE(xlen));
  action = (action_t) get_field(val, MCONTROL_ACTION);
  chain_bit = get_field(val, MCONTROL_CHAIN);
  match = (match_t) get_field(val, MCONTROL_MATCH);
  m = get_field(val, MCONTROL_M);
  s = get_field(val, MCONTROL_S);
  u = get_field(val, MCONTROL_U);
  execute = get_field(val, MCONTROL_EXECUTE);
  store = get_field(val, MCONTROL_STORE);
  load = get_field(val, MCONTROL_LOAD);

  if (execute)
    timing = 0;

  // Assume we're here because of csrw.
  proc->trigger_updated();
  return true;
}

reg_t mcontrol_t::tdata2_read(processor_t *proc) const
{
  D(fprintf(stderr, "tdata2 read -> 0x%lx\n", tdata2));
  return tdata2;
}

bool mcontrol_t::tdata2_write(processor_t *proc, reg_t val)
{
  D(fprintf(stderr, "tdata2 write 0x%lx\n", val));
  if (dmode && !proc->get_state()->debug_mode) {
    return false;
  }
  tdata2 = val;
  return true;
}

};
