/*
 * Execution Context (EC)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * Copyright (C) 2019-2024 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#pragma once

#include "atomic.hpp"
#include "kmem.hpp"
#include "kobject.hpp"
#include "lock_guard.hpp"
#include "pd.hpp"
#include "queue.hpp"
#include "regs.hpp"
#include "sc.hpp"
#include "scheduler.hpp"
#include "timeout_hypercall.hpp"

class Fpu;
class Utcb;

class Ec : public Kobject, private Queue<Sc>, public Queue<Ec>::Element
{
    friend class Ec_arch;
    friend class Tlb;

    private:
        using cont_t = void (*)(Ec *);  // Continuation Type

        Cpu_regs            regs;
        unsigned long const evt;
        cpu_t         const cpu;
        Fpu *         const fpu;
        void *        const kpage;
        Ec *                callee      { nullptr };
        Ec *                caller      { nullptr };
        Atomic<cont_t>      cont        { nullptr };
        Timeout_hypercall   timeout     { this };
        Spinlock            lock;

        static Atomic<Ec *> current asm ("current") CPULOCAL;
        static Ec *         fpowner                 CPULOCAL;
        static unsigned     donations               CPULOCAL;
        static Slab_cache   cache;

        ALWAYS_INLINE inline auto &cpu_regs() { return regs; }
        ALWAYS_INLINE inline auto &exc_regs() { return regs.exc; }
        ALWAYS_INLINE inline auto &sys_regs() { return regs.exc.sys; }

        ALWAYS_INLINE
        inline bool is_vcpu() const { return subtype >= Kobject::Subtype::EC_VCPU_REAL; }

        auto get_utcb() const { return static_cast<Utcb *>(kpage); }

        ALWAYS_INLINE NONNULL
        inline void set_partner (Ec *e)
        {
            callee = e;
            callee->caller = this;
            donations++;
        }

        ALWAYS_INLINE
        inline bool clr_partner()
        {
            callee->caller = nullptr;
            callee = nullptr;
            return donations--;
        }

        void fpu_load();
        void fpu_save();

        NOINLINE
        void handle_hazard (unsigned, cont_t);

        NOINLINE
        void help (Ec *, cont_t);

        ALWAYS_INLINE
        inline void rendezvous (Ec *, cont_t, cont_t, uintptr_t, uintptr_t, uintptr_t);

        [[noreturn]] HOT
        void reply (cont_t = nullptr);

        [[noreturn]]
        void kill (char const *);

        [[noreturn]]
        static void dead (Ec *self) { self->kill ("IPC Abort"); }

        [[noreturn]]
        static void blocking (Ec *self) { self->kill ("Blocking"); }

        [[noreturn]]
        static void idle (Ec *);

        [[noreturn]] HOT
        static void recv_kern (Ec *);

        [[noreturn]] HOT
        static void recv_user (Ec *);

        template<cont_t> [[noreturn]] static void send_msg (Ec *);

        [[noreturn]] HOT
        static void sys_ipc_call (Ec *);

        [[noreturn]] HOT
        static void sys_ipc_reply (Ec *);

        [[noreturn]]
        static void sys_create_pd (Ec *);

        [[noreturn]]
        static void sys_create_ec (Ec *);

        [[noreturn]]
        static void sys_create_sc (Ec *);

        [[noreturn]]
        static void sys_create_pt (Ec *);

        [[noreturn]]
        static void sys_create_sm (Ec *);

        [[noreturn]]
        static void sys_ctrl_pd (Ec *);

        [[noreturn]]
        static void sys_ctrl_ec (Ec *);

        [[noreturn]]
        static void sys_ctrl_sc (Ec *);

        [[noreturn]]
        static void sys_ctrl_pt (Ec *);

        [[noreturn]]
        static void sys_ctrl_sm (Ec *);

        [[noreturn]]
        static void sys_ctrl_hw (Ec *);

        [[noreturn]]
        static void sys_assign_int (Ec *);

        [[noreturn]]
        static void sys_assign_dev (Ec *);

        [[noreturn]]
        void sys_finish_status (Status);

        // Constructor: Kernel Thread
        Ec (Refptr<Space_obj> &ref_obj, Refptr<Space_hst> &ref_hst, Refptr<Space_pio> &ref_pio, cpu_t c, cont_t x) : Kobject (Kobject::Type::EC, Kobject::Subtype::EC_GLOBAL), regs (ref_obj, ref_hst, ref_pio), evt (0), cpu (c), fpu (nullptr), kpage (nullptr), cont (x) { ref_inc(); }

        // Constructor: HST EC
        Ec (bool t, Fpu *f, Refptr<Space_obj> &ref_obj, Refptr<Space_hst> &ref_hst, Refptr<Space_pio> &ref_pio, void *k, cpu_t c, unsigned long e, cont_t x) : Kobject (Kobject::Type::EC, t ? Kobject::Subtype::EC_GLOBAL : Kobject::Subtype::EC_LOCAL), regs (ref_obj, ref_hst, ref_pio), evt (e), cpu (c), fpu (f), kpage (k), cont (x) {}

        // Constructor: GST EC
        template<typename T> Ec (bool t, Fpu *f, Refptr<Space_obj> &ref_obj, Refptr<Space_hst> &ref_hst, T *v, void *k, cpu_t c, unsigned long e, cont_t x) : Kobject (Kobject::Type::EC, t ? Kobject::Subtype::EC_VCPU_OFFS : Kobject::Subtype::EC_VCPU_REAL), regs (ref_obj, ref_hst, v), evt (e), cpu (c), fpu (f), kpage (k), cont (x) {}

    public:
        // Factory: Kernel Thread
        [[nodiscard]] static Ec *create (cpu_t, cont_t);

        // Factory: HST EC
        [[nodiscard]] static Ec *create_hst (Status &s, Pd *, bool, bool, cpu_t, unsigned long, uintptr_t, uintptr_t);

        // Factory: GST EC
        [[nodiscard]] static Ec *create_gst (Status &s, Pd *, bool, bool, cpu_t, unsigned long, uintptr_t, uintptr_t);

        void destroy()
        {
            this->~Ec();

            operator delete (this, cache);
        }

        static void create_idle();
        static void create_root();

        static bool switch_fpu (Ec *);

        ALWAYS_INLINE
        static inline Ec *remote_current (cpu_t cpu)
        {
            return *Kmem::loc_to_glob (cpu, &current);
        }

        /*
         * Mark the EC as blocked using a sentinel continuation
         *
         * Ordering: RELAXED because on the same CPU as blocked()
         */
        ALWAYS_INLINE
        inline void block() { cont.store (blocking, __ATOMIC_RELAXED); }

        /*
         * Mark the EC as unblocked using a non-sentinel continuation
         *
         * Ordering: RELEASE to synchronize with a concurrent blocked() on a different CPU, RELAXED if on the same CPU as blocked()
         */
        ALWAYS_INLINE
        inline void unblock (cont_t c, bool same_cpu) { cont.store (c, same_cpu ? __ATOMIC_RELAXED : __ATOMIC_RELEASE); }

        /*
         * Determine if the EC is blocked
         *
         * Ordering: ACQUIRE to synchronize with a concurrent unblock() on a different CPU
         */
        ALWAYS_INLINE
        inline bool blocked() const { cont_t c = cont.load (__ATOMIC_ACQUIRE); return c == blocking || c == nullptr; }

        /*
         * Core X               Core Y
         * e.g. Sm::dn()        e.g. Sm::up()
         *
         * A: ec->block()       C: ec->unblock()
         * B: ec->block_sc()    D: ec->unblock_sc()
         *
         * Ordering: A before B, C before D, A before C, B+D can't run in parallel
         *
         * @return true if B happened before C, false if B happened after C
         */
        [[nodiscard]]
        bool block_sc()
        {
            {   Lock_guard <Spinlock> guard { lock };

                // If C already happened, then don't block the SC
                if (!blocked())
                    return false;

                // Otherwise D will later unblock the SC
                enqueue_tail (Scheduler::get_current());
            }

            return true;
        }

        ALWAYS_INLINE
        void unblock_sc()
        {
            Lock_guard <Spinlock> guard { lock };

            for (Sc *sc; (sc = dequeue_head()); Scheduler::unblock (sc)) ;
        }

        ALWAYS_INLINE
        inline void set_timeout (uint64_t t, Sm *s)
        {
            timeout.enqueue (t, s);
        }

        ALWAYS_INLINE
        inline void clr_timeout()
        {
            timeout.dequeue();
        }

        void activate();

        void adjust_offset_ticks (uint64_t);

        template<Status S, bool T = false> [[noreturn]] NOINLINE static void sys_finish (Ec *);

        static cont_t const syscall[16] asm ("syscall");
};
