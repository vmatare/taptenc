#pragma once

#include "../constraints/constraints.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace taptenc {

struct state {
  ::std::string id;
  ::std::string inv;
  bool urgent;
  bool initial;
  state(::std::string arg_id, ::std::string arg_inv, bool arg_urgent = false,
        bool arg_initial = false);
  bool operator<(const state &r) const;
};
typedef struct state State;

struct transition {
  ::std::string source_id;
  ::std::string dest_id;
  ::std::string action;
  ::std::string guard;
  ::std::string update;
  ::std::string sync;
  bool passive;
  transition(::std::string arg_source_id, ::std::string arg_dest_id,
             std::string arg_action, ::std::string arg_guard,
             ::std::string arg_update, ::std::string arg_sync,
             bool arg_passive = false);

  bool operator<(const transition &r) const;
};
typedef struct transition Transition;

struct automaton {
  ::std::vector<State> states;
  ::std::vector<Transition> transitions;
  ::std::vector<::std::string> clocks;
  ::std::vector<::std::string> bool_vars;
  ::std::string prefix;

  automaton(::std::vector<State> arg_states,
            ::std::vector<Transition> arg_transitions, ::std::string arg_prefix,
            bool setTrap = true);
};
typedef struct automaton Automaton;


enum ChanType { Binary = 1, Broadcast = 0 };
struct channel {
  ChanType type;
  ::std::string name;
  channel(ChanType arg_type, ::std::string arg_name) {
    type = arg_type;
    name = arg_name;
  }
};
typedef struct channel Channel;

struct automataGlobals {
  ::std::vector<::std::string> clocks;
  ::std::vector<::std::string> bool_vars;
  ::std::vector<Channel> channels;
};
typedef struct automataGlobals AutomataGlobals;

struct automataSystem {
  ::std::vector<::std::pair<Automaton, ::std::string>> instances;
  AutomataGlobals globals;
};
typedef struct automataSystem AutomataSystem;

struct tlEntry {
  Automaton ta;
  ::std::vector<Transition> trans_out;
  tlEntry(Automaton &arg_ta, ::std::vector<Transition> arg_trans_out);
};
typedef struct tlEntry TlEntry;

typedef ::std::unordered_map<::std::string, TlEntry> TimeLine;
typedef ::std::unordered_map<::std::string, TimeLine> TimeLines;
} // end namespace taptenc
