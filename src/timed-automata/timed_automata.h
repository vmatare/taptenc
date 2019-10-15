#pragma once
#include "../utils.h"
#include "constraints.h"
#include <algorithm>
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
        bool arg_initial = false) {
    id = arg_id;
    inv = arg_inv;
    urgent = arg_urgent;
    initial = arg_initial;
  }
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
             bool arg_passive = false) {
    source_id = arg_source_id;
    dest_id = arg_dest_id;
    action = arg_action;
    guard = arg_guard;
    update = arg_update;
    sync = arg_sync;
    passive = arg_passive;
  }

  bool operator<(const transition &r) const;
};
typedef struct transition Transition;

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

struct automaton {
  ::std::vector<State> states;
  ::std::vector<Transition> transitions;
  ::std::vector<::std::string> clocks;
  ::std::vector<::std::string> bool_vars;
  ::std::string prefix;

  automaton(::std::vector<State> arg_states,
            ::std::vector<Transition> arg_transitions, ::std::string arg_prefix,
            bool setTrap = true) {
    states = arg_states;
    if (setTrap == true && states.end() == find_if(states.begin(), states.end(),
                                                   [](const State &s) -> bool {
                                                     return s.id == "trap";
                                                   })) {
      states.push_back(State("trap", ""));
    }
    transitions = arg_transitions;
    prefix = arg_prefix;
  }
};
typedef struct automaton Automaton;

struct automataSystem {
  ::std::vector<::std::pair<Automaton, ::std::string>> instances;
  AutomataGlobals globals;
};
typedef struct automataSystem AutomataSystem;

struct tlEntry {
  Automaton ta;
  ::std::vector<Transition> trans_out;
  tlEntry(Automaton &arg_ta, ::std::vector<Transition> arg_trans_out)
      : ta(arg_ta), trans_out(arg_trans_out){};
};
typedef struct tlEntry TlEntry;

typedef ::std::unordered_map<::std::string, TlEntry> TimeLine;
typedef ::std::unordered_map<::std::string, TimeLine> TimeLines;
} // end namespace taptenc
