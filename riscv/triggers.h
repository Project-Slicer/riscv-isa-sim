#ifndef _RISCV_TRIGGERS_H
#define _RISCV_TRIGGERS_H

#include <vector>

#include "decode.h"

class processor_t;

namespace trigger {

typedef enum {
  OPERATION_EXECUTE,
  OPERATION_STORE,
  OPERATION_LOAD,
} operation_t;

typedef enum {
  MATCH_NONE,
  MATCH_FIRE_BEFORE,
  MATCH_FIRE_AFTER
} match_result_t;

typedef enum {
  ACTION_DEBUG_EXCEPTION = MCONTROL_ACTION_DEBUG_EXCEPTION,
  ACTION_DEBUG_MODE = MCONTROL_ACTION_DEBUG_MODE,
  ACTION_TRACE_START = MCONTROL_ACTION_TRACE_START,
  ACTION_TRACE_STOP = MCONTROL_ACTION_TRACE_STOP,
  ACTION_TRACE_EMIT = MCONTROL_ACTION_TRACE_EMIT
} action_t;

class trigger_t {
  public:
    bool dmode;
    action_t action;

    virtual bool checks_execute() const { return false; }
    virtual bool checks_load() const { return false; }
    virtual bool checks_store() const { return false; }

    /* Return value of the chain bit, if the trigger supports it. */
    virtual bool chain() const { return false; }

    virtual reg_t tdata1_read(processor_t *proc) const = 0;
    virtual bool tdata1_write(processor_t *proc, reg_t val) = 0;
    virtual reg_t tdata2_read(processor_t *proc) const { return 0; };
    virtual bool tdata2_write(processor_t *proc, reg_t val) {
      /* Default triggers don't implement tdata2. Since the registers is WARL, we
       * just ignore the write. */
      return true;
    };
    virtual match_result_t memory_access_match(
        processor_t *proc, operation_t operation, reg_t address, reg_t data)
    {
      return MATCH_NONE;
    };

  protected:
    trigger_t() : dmode(false), action(ACTION_DEBUG_EXCEPTION) {};
};

class mcontrol_t : public trigger_t {
  /* Combined class for mcontrol and mcontrol6. */
  public:
    mcontrol_t() : mcontrol6(false), vs(false), vu(false), hit(false),
                   select(false), timing(false), size(0),
                   chain_bit(false),
                   match(MATCH_EQUAL), m(false), s(false), u(false),
                   execute(false), store(false), load(false), tdata2(0) {};

    bool simple_match(unsigned xlen, reg_t value);
    virtual bool chain() const override { return chain_bit; }

    typedef enum {
      MATCH_EQUAL = MCONTROL_MATCH_EQUAL,
      MATCH_NAPOT = MCONTROL_MATCH_NAPOT,
      MATCH_GE = MCONTROL_MATCH_GE,
      MATCH_LT = MCONTROL_MATCH_LT,
      MATCH_MASK_LOW = MCONTROL_MATCH_MASK_LOW,
      MATCH_MASK_HIGH = MCONTROL_MATCH_MASK_HIGH
    } match_t;

    bool mcontrol6;
    bool vs;
    bool vu;
    bool hit;
    bool select;
    bool timing;
    uint8_t size;
    bool chain_bit;
    match_t match;
    bool m;
    bool s;
    bool u;
    bool execute;
    bool store;
    bool load;
    reg_t tdata2;

    virtual bool checks_execute() const override { return execute; }
    virtual bool checks_load() const override { return load; }
    virtual bool checks_store() const override { return store; }

    virtual reg_t tdata1_read(processor_t *proc) const override;
    virtual bool tdata1_write(processor_t *proc, reg_t val) override;
    virtual reg_t tdata2_read(processor_t *proc) const override;
    virtual bool tdata2_write(processor_t *proc, reg_t val) override;

    virtual match_result_t memory_access_match(
        processor_t *proc, operation_t operation, reg_t address, reg_t data) override;
};

class module_t {
  public:
    processor_t *proc;

    module_t(unsigned count);

    match_result_t memory_access_match(
        action_t *action, operation_t operation, reg_t address, reg_t data);

    trigger_t *trigger(unsigned index) {
      return triggers[index];
    }

    unsigned count() const {
      return triggers.size();
    }

    // TODO: Refactor processor_t::trigger_updated() so we can make this
    // private.
    std::vector<trigger_t *> triggers;
};

};

#endif