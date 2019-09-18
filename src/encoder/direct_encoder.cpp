#include "direct_encoder.h"
#include "constants.h"
#include "encoder.h"
#include "filter.h"
#include "timed_automata.h"
#include "utils.h"
#include "vis_info.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#define CONTEXT 2

using namespace taptenc;
void DirectEncoder::printTLs(const TimeLines &to_print) {
  std::cout << std::endl;
  for (const auto &entry : to_print) {
    std::cout << "TL: " << entry.first << std::endl;
    for (const auto &ta : entry.second) {
      std::cout << "    TA: " << ta.first << std::endl;
    }
  }
  std::cout << std::endl;
}

OrigMap DirectEncoder::createOrigMapping(const TimeLines &orig_tls,
                                         std::string prefix) {
  OrigMap res;
  for (const auto &curr_tl : orig_tls) {
    for (const auto &tl_entry : curr_tl.second) {
      if (tl_entry.second.ta.prefix == constants::QUERY) {
        continue;
      }
      std::string ta_prefix =
          prefix == "" ? tl_entry.second.ta.prefix
                       : addToPrefix(tl_entry.second.ta.prefix, prefix);
      res[ta_prefix] = tl_entry.second.ta.prefix;
    }
  }
  return res;
}

TimeLines DirectEncoder::createWindow(const TimeLines &orig_tls,
                                      std::string start_pa, std::string end_pa,
                                      const Filter &base_filter,
                                      const Filter &target_filter,
                                      std::string prefix, std::string clock,
                                      const EncICInfo &info) {
  TimeLines new_window;
  auto start_pa_entry = std::find(pa_order.begin(), pa_order.end(), start_pa);
  if (start_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder createWindow: could not find start pa "
              << start_pa << std::endl;
    return new_window;
  }
  auto end_pa_entry = std::find(start_pa_entry, pa_order.end(), end_pa);
  if (end_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder createWindow: could not find end pa " << end_pa
              << std::endl;
    return new_window;
  }
  std::size_t context_start = start_pa_entry - pa_order.begin();
  std::size_t context_end = end_pa_entry - pa_order.begin();
  bool upper_bounded =
      (info.bounds.upper_bound != std::numeric_limits<int>::max());
  auto curr_tl = orig_tls.find(*(pa_order.begin() + context_start));
  std::string op_name = Filter::getPrefix(prefix, constants::CONSTRAINT_SEP);
  if (curr_tl != orig_tls.end()) {
    // iterate over orig TLs of window
    int tls_copied = 0;
    while (context_start + tls_copied <= context_end) {
      TimeLine copy_tls;
      // formulate clock constraints
      TimeLine new_tls;
      // copy all original tls
      for (auto &tl_entry : curr_tl->second) {
        std::string ta_prefix = addToPrefix(tl_entry.second.ta.prefix, prefix);
        Automaton copy_ta = target_filter.filterAutomaton(tl_entry.second.ta,
                                                          ta_prefix, "", false);
        copy_ta.clocks.push_back(clock);
        if (upper_bounded) {
          addInvariants(copy_ta, base_filter.getFilter(),
                        clock + info.bounds.r_op +
                            std::to_string(info.bounds.upper_bound));
        }
        std::vector<Transition> cp_to_other_cp;
        // also copy the transitions connecting different orig TLs
        if (tls_copied + context_start < context_end) {
          cp_to_other_cp =
              addToPrefixOnTransitions(tl_entry.second.trans_out, op_name);
        } else {
          cp_to_other_cp = addToPrefixOnTransitions(tl_entry.second.trans_out,
                                                    op_name, true, false);
        }
        target_filter.filterTransitionsInPlace(cp_to_other_cp, "", true);
        auto emp = new_tls.emplace(
            std::make_pair(ta_prefix, TlEntry(copy_ta, cp_to_other_cp)));
        if (emp.second == false) {
          std::cout << "DirectEncoder createWindow: failed to add prefix: "
                    << prefix << std::endl;
        }
      }
      // insert the new tls and also save them in the curr_window
      for (const auto &new_tl : new_tls) {
        new_window[Filter::getPrefix(new_tl.first, constants::TL_SEP)].emplace(
            new_tl);
      }
      tls_copied++;
      if (context_start + tls_copied < pa_order.size()) {
        curr_tl = orig_tls.find(pa_order[context_start + tls_copied]);
        if (curr_tl == orig_tls.end()) {
          std::cout << "DirectEncoder createWindow: cannot find next tl"
                    << std::endl;
        }
      } else {
        curr_tl = orig_tls.end();
      }
    }
  } else {
    std::cout << "DirectEncoder createWindow: done cannot find start tl "
                 "of chain "
              << info.name << " context start pa "
              << *(pa_order.begin() + context_start) << std::endl;
  }
  return new_window;
}

void DirectEncoder::createTransitionsBetweenWindows(
    const Automaton &base_ta, TimeLines &source_tls, TimeLines &dest_tls,
    const std::unordered_map<std::string, std::string> &map_to_orig,
    std::string start_pa, std::string end_pa, std::string guard,
    std::string update) {
  auto start_pa_entry = std::find(pa_order.begin(), pa_order.end(), start_pa);
  if (start_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder createTransitionsBetweenWindows: could not "
                 "find start pa "
              << start_pa << std::endl;
    return;
  }
  auto end_pa_entry = std::find(start_pa_entry, pa_order.end(), end_pa);
  if (end_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder createTransitionsBetweenWindows: could not "
                 "find end pa "
              << end_pa << std::endl;
    return;
  }
  std::size_t context_start = start_pa_entry - pa_order.begin();
  std::size_t context_end = end_pa_entry - pa_order.begin();
  auto source_tl = source_tls.find(*(pa_order.begin() + context_start));
  int i = 0;
  while (context_start + i <= context_end && source_tl != source_tls.end()) {
    auto dest_tl = dest_tls.find(*(pa_order.begin() + context_start + i));
    if (dest_tl != dest_tls.end()) {
      for (auto &source_entry : source_tl->second) {
        auto dest_entry = std::find_if(
            dest_tl->second.begin(), dest_tl->second.end(),
            [source_entry,
             map_to_orig](const std::pair<std::string, TlEntry> &d) bool {
              auto orig_source_entry = map_to_orig.find(source_entry.first);
              auto orig_dest_entry = map_to_orig.find(d.first);
              return orig_source_entry != map_to_orig.end() &&
                     orig_dest_entry != map_to_orig.end() &&
                     orig_source_entry->second == orig_dest_entry->second;
            });
        if (dest_entry != dest_tl->second.end()) {
          std::vector<Transition> res;
          res = createCopyTransitionsBetweenTAs(
              source_entry.second.ta, dest_entry->second.ta,
              dest_entry->second.ta.states, guard, update, "");
          std::vector<Transition> res_succ =
              createSuccessorTransitionsBetweenTAs(
                  base_ta, source_entry.second.ta, dest_entry->second.ta,
                  source_entry.second.ta.states, guard, update);
          source_entry.second.trans_out.insert(
              source_entry.second.trans_out.end(), res.begin(), res.end());
          source_entry.second.trans_out.insert(
              source_entry.second.trans_out.end(), res_succ.begin(),
              res_succ.end());
        }
      }
    }
    i++;
    source_tl = source_tls.find(*(pa_order.begin() + context_start + i));
  }
}

void DirectEncoder::mergeWindows(TimeLines &dest, const TimeLines &to_add,
                                 bool overwrite) {
  for (const auto &tl : to_add) {
    auto dest_tl = dest.find(tl.first);
    if (dest_tl == dest.end()) {
      dest.insert(tl);
    } else {
      for (const auto &tl_entry : tl.second) {
        auto dest_tl_entry = dest_tl->second.find(tl_entry.first);
        if (dest_tl_entry == dest_tl->second.end()) {
          dest_tl->second.insert(tl_entry);
        } else if (overwrite) {
          dest_tl_entry->second = tl_entry.second;
        } else {
          // skip
        }
      }
    }
  }
}

void DirectEncoder::generateBaseTimeLine(AutomataSystem &s,
                                         const int base_index,
                                         const int plan_index) {
  std::cout << "DirectEncoder generateBaseTimeLine: Assuming plan automaton "
               "states are sorted by plan order!"
            << std::endl;
  Filter base_filter = Filter(s.instances[base_index].first.states);
  for (const auto &pa : s.instances[plan_index].first.states) {
    TimeLine tl;
    std::string ta_prefix = toPrefix("", "", pa.id);
    Automaton ta_copy = base_filter.filterAutomaton(
        s.instances[base_index].first, ta_prefix, "", false);
    if (pa.initial) {
      auto search = std::find_if(ta_copy.states.begin(), ta_copy.states.end(),
                                 [](const State &s) bool { return s.initial; });
      if (search != ta_copy.states.end()) {
        search->initial = true;
        std::cout << "DirectEncoder generateBaseTimeLine: Set initial state: "
                  << search->id << std::endl;
      }
    } else {
      for (auto &s : ta_copy.states) {
        s.initial = false;
      }
    }
    ta_copy.clocks.insert(ta_copy.clocks.end(),
                          s.instances[plan_index].first.clocks.begin(),
                          s.instances[plan_index].first.clocks.end());
    auto emp_ta = tl.emplace(
        std::make_pair(ta_prefix, TlEntry(ta_copy, std::vector<Transition>())));
    if (emp_ta.second == true) {
      addInvariants(emp_ta.first->second.ta, emp_ta.first->second.ta.states,
                    pa.inv);
    } else {
      std::cout << "DirectEncoder generateBaseTimeLine: plan automaton has non "
                   "unique id (id "
                << pa.id << ")" << std::endl;
    }
    pa_order.push_back(pa.id);
    auto emp_tl = pa_tls.emplace(std::make_pair(pa.id, tl));
    if (emp_tl.second == false) {
      std::cout << "DirectEncoder generateBaseTimeLine: plan action timeline "
                   "already present (pa "
                << pa.id << ")" << std::endl;
    }
  }
  // generate query state
  TimeLine query_tl;
  Automaton query_ta(std::vector<State>(), std::vector<Transition>(),
                     constants::QUERY, false);
  query_ta.states.push_back(State(constants::QUERY, ""));
  query_tl.emplace(std::make_pair(
      constants::QUERY, TlEntry(query_ta, std::vector<Transition>())));
  auto emp_tl = pa_tls.emplace(std::make_pair(constants::QUERY, query_tl));
  if (emp_tl.second == false) {
    std::cout << "DirectEncoder generateBaseTimeLine: query timeline "
                 "already present (no plan action can be named query) "
              << std::endl;
  }
  // make transitions from last plan actions to query
  for (auto &last_tl :
       pa_tls.find(s.instances[plan_index].first.states.back().id)->second) {
    for (auto &s : last_tl.second.ta.states) {
      last_tl.second.trans_out.push_back(
          Transition(s.id, constants::QUERY, "", "", "", ""));
    }
  }
  // generate connections between TLs according to plan TA transitions
  for (const auto &pa_trans : s.instances[plan_index].first.transitions) {
    auto source_ta = pa_tls.find(pa_trans.source_id);
    auto dest_ta = pa_tls.find(pa_trans.dest_id);
    if (source_ta != pa_tls.end() && dest_ta != pa_tls.end()) {
      std::string source_ta_prefix = toPrefix("", "", pa_trans.source_id);
      std::string dest_ta_prefix = toPrefix("", "", pa_trans.dest_id);
      std::vector<Transition> successor_trans = createCopyTransitionsBetweenTAs(
          source_ta->second.find(source_ta_prefix)->second.ta,
          dest_ta->second.find(dest_ta_prefix)->second.ta,
          source_ta->second.find(source_ta_prefix)->second.ta.states,
          pa_trans.guard, pa_trans.update, "");
      pa_tls[pa_trans.source_id]
          .find(source_ta_prefix)
          ->second.trans_out.insert(pa_tls[pa_trans.source_id]
                                        .find(source_ta_prefix)
                                        ->second.trans_out.end(),
                                    successor_trans.begin(),
                                    successor_trans.end());
    } else {
      std::cout << "DirectEncoder generateBaseTimeLine: pa "
                << pa_trans.source_id << " or " << pa_trans.dest_id
                << " has no timeline yet" << std::endl;
    }
  }
}

void DirectEncoder::addOutgoingTransOfOrigTL(const TimeLine &orig_tl,
                                             TimeLine &new_tl,
                                             const OrigMap &to_orig,
                                             std::string guard) {
  for (auto &tl_entry : new_tl) {
    auto orig_name = to_orig.find(tl_entry.first);
    if (orig_name != to_orig.end()) {
      const auto &orig_entry = orig_tl.find(orig_name->second);
      if (orig_entry != orig_tl.end()) {
        for (const auto &tr : orig_entry->second.trans_out) {
          if (Filter::getPrefix(tr.source_id, constants::TL_SEP) !=
              Filter::getPrefix(tr.dest_id, constants::TL_SEP)) {
            std::string source_base_name =
                Filter::getSuffix(tr.source_id, constants::BASE_SEP);
            const auto &source_state = std::find_if(
                tl_entry.second.ta.states.begin(),
                tl_entry.second.ta.states.end(),
                [source_base_name](const State &s) bool {
                  return Filter::getSuffix(s.id, constants::BASE_SEP) ==
                         source_base_name;
                });
            if (source_state != tl_entry.second.ta.states.end()) {
              tl_entry.second.trans_out.push_back(Transition(
                  tl_entry.first +
                      Filter::getSuffix(tr.source_id, constants::BASE_SEP),
                  tr.dest_id, tr.action, addConstraint(tr.guard, guard),
                  tr.update, tr.sync));
            }
          }
        }
      } else {
        std::cout << "DirectEncoder addOutgoingTransOfOrigTL: cannot find orig "
                     "tl entry "
                  << orig_name->second << std::endl;
      }
    } else {
      std::cout
          << "DirectEncoder addOutgoingTransOfOrigTL: orig mapping not found: "
          << tl_entry.first << std::endl;
    }
  }
}

std::vector<Transition> DirectEncoder::createTransitionsBackToOrigTL(
    const std::vector<Transition> &trans, std::string prefix, std::string pa,
    std::string guard) {
  std::vector<Transition> res;
  for (const auto &tr : trans) {
    if (pa + Filter::getSuffix(tr.source_id, constants::BASE_SEP) ==
            tr.source_id &&
        pa + Filter::getSuffix(tr.dest_id, constants::BASE_SEP) != pa) {
      res.push_back(Transition(
          prefix + Filter::getSuffix(tr.source_id, constants::BASE_SEP),
          tr.dest_id, tr.action, addConstraint(tr.guard, guard), tr.update,
          tr.sync));
    }
  }
  return res;
}

std::vector<Transition>
DirectEncoder::addToPrefixOnTransitions(const std::vector<Transition> &trans,
                                        std::string to_add, bool on_inner_trans,
                                        bool on_outgoing_trans) {
  std::vector<Transition> res;
  for (const auto &tr : trans) {
    bool is_inner = Filter::getPrefix(tr.source_id, constants::TL_SEP) ==
                    Filter::getPrefix(tr.dest_id, constants::TL_SEP);
    if ((is_inner && on_inner_trans) || (!is_inner && on_outgoing_trans)) {
      res.push_back(Transition(addToPrefix(tr.source_id, to_add),
                               addToPrefix(tr.dest_id, to_add), tr.action,
                               tr.guard, tr.update, tr.sync));
    }
  }
  return res;
}

void DirectEncoder::modifyTransitionsToNextTl(
    std::vector<Transition> &trans, std::string curr_pa, std::string guard,
    std::string update, std::string sync, std::string op_name) {
  for (auto &t : trans) {
    if (Filter::getPrefix(t.dest_id, constants::TL_SEP) != curr_pa) {
      t.guard = addConstraint(t.guard, guard);
      t.update = addUpdate(t.update, update);
      if (sync != "")
        t.sync = sync;
      if (op_name != "") {
        t.dest_id = addToPrefix(t.dest_id, op_name);
      }
    }
  }
}

void DirectEncoder::removeTransitionsToNextTl(std::vector<Transition> &trans,
                                              std::string curr_pa) {
  trans.erase(std::remove_if(trans.begin(), trans.end(),
                             [curr_pa](Transition &t) bool {
                               return Filter::getPrefix(t.dest_id,
                                                        constants::TL_SEP) !=
                                      curr_pa;
                             }),
              trans.end());
}

std::pair<int, int> DirectEncoder::calculateContext(const EncICInfo &info,
                                                    std::string starting_pa,
                                                    std::string ending_pa,
                                                    int lb_offset,
                                                    int ub_offset) {
  // start index needs to subtract one because of start action
  // indices w.r.t. to the plan, not the plan automaton
  int start_index = stoi(Filter::getSuffix(starting_pa, constants::PA_SEP)) - 1;
  // if an end_index is specified this means the begin of the PA ends the
  // context, hence we have to subtract 1 for the start action and 1 to exclude
  // the ending pa itself
  int end_index =
      (ending_pa == "")
          ? plan.size() - 1
          : stoi(Filter::getSuffix(ending_pa, constants::PA_SEP)) - 2;
  int offset_index = start_index;
  if ((long unsigned int)start_index >= plan.size()) {
    std::cout << "DirectEncoder calculateContext: starting pa " << starting_pa
              << " is out of range" << std::endl;
    return std::make_pair(0, 0);
  }
  if (info.type == ICType::Future) {
    int lb_acc = 0;
    int ub_acc = 0;
    for (auto pa = plan.begin() + start_index; pa != plan.end(); ++pa) {
      lb_acc += pa->duration.lower_bound;
      if (ub_acc != std::numeric_limits<int>::max()) {
        // Increase ub_acc only if it does not overflow
        ub_acc = safeAddition(pa->duration.upper_bound, ub_acc);
      }
      if (ub_acc < lb_offset) {
        offset_index++;
      }
      if (lb_acc >= safeAddition(info.bounds.upper_bound, ub_offset) ||
          pa - plan.begin() == end_index) {
        return std::make_pair(offset_index + 1,
                              pa - plan.begin() - offset_index);
      }
      if (pa - plan.begin() == end_index) {
        break;
      }
    }
    // res needs to add one because of fin action
    return std::make_pair(offset_index + 1, end_index - offset_index);
  } else {
    std::cout << "DirectEncoder calculateContext: unsopported type "
              << info.type << std::endl;
    return std::make_pair(0, 0);
  }
}

void DirectEncoder::encodeInvariant(AutomataSystem &,
                                    const std::vector<State> &targets,
                                    const std::string pa) {
  Filter target_filter = Filter(targets);
  auto search_tl = pa_tls.find(pa);
  if (search_tl != pa_tls.end()) {
    auto search_pa = std::find(pa_order.begin(), pa_order.end(), pa);
    if (search_pa == pa_order.end()) {
      std::cout << "DirectEncoder encodeInvariant: could not find pa " << pa
                << std::endl;
      return;
    }
    // restrict transitions from prev tl to target states
    if (search_pa - pa_order.begin() > 0) {
      for (auto &prev_tl_entry :
           pa_tls[pa_order[search_pa - pa_order.begin() - 1]]) {
        target_filter.filterTransitionsInPlace(prev_tl_entry.second.trans_out,
                                               pa, false);
      }
    }
    for (auto &tl_entry : search_tl->second) {
      target_filter.filterAutomatonInPlace(tl_entry.second.ta, "");
      target_filter.filterTransitionsInPlace(tl_entry.second.trans_out, pa,
                                             true);
    }
  } else {
    std::cout
        << " DirectEncoder encodeInvariant: could not find timeline of pa "
        << pa << std::endl;
  }
}

void DirectEncoder::encodeNoOp(AutomataSystem &,
                               const std::vector<State> &targets,
                               const std::string pa) {
  Filter target_filter = Filter(targets);
  auto search_tl = pa_tls.find(pa);
  if (search_tl != pa_tls.end()) {
    auto search_pa = std::find(pa_order.begin(), pa_order.end(), pa);
    if (search_pa == pa_order.end()) {
      std::cout << "DirectEncoder encodeNoOp: could not find pa " << pa
                << std::endl;
      return;
    }
    // restrict transitions from prev tl to target states
    if (search_pa - pa_order.begin() > 0) {
      for (auto &prev_tl_entry :
           pa_tls[pa_order[search_pa - pa_order.begin() - 1]]) {
        target_filter.filterTransitionsInPlace(prev_tl_entry.second.trans_out,
                                               pa, false);
      }
    }
  } else {
    std::cout << "DirectEncoder encodeNoOp: could not find timeline of pa "
              << pa << std::endl;
  }
}

void DirectEncoder::encodeUntilChain(AutomataSystem &s,
                                     const std::vector<EncICInfo> &infos,
                                     const std::string start_pa,
                                     const std::string end_pa,
                                     const int base_index) {
  if (infos.size() == 0) {
    std::cout << "DirectEncoder enodeUntilchain: empty infos, abort."
              << std::endl;
  }
  Filter base_filter = Filter(s.instances[base_index].first.states);
  std::string clock = "clX" + infos[0].name;
  auto start_pa_entry = std::find(pa_order.begin(), pa_order.end(), start_pa);
  if (start_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder encodeUntilChain: could not find start pa "
              << start_pa << std::endl;
    return;
  }
  auto end_pa_entry = std::find(start_pa_entry, pa_order.end(), end_pa);
  if (end_pa_entry == pa_order.end()) {
    std::cout << "DirectEncoder encodeUntilChain: could not find end pa "
              << end_pa << std::endl;
    return;
  }
  int lb_acc = 0;
  int ub_acc = 0;
  // TLs before encoding the Until Chain
  TimeLines orig_tls = pa_tls;
  // TLs of the current window
  TimeLines curr_window = pa_tls;
  // TL of the previous window
  TimeLines prev_window;
  // maps to obtain the original tl entry id given the prefix of
  // current/previous window prefix id
  OrigMap prev_to_orig;
  OrigMap curr_to_orig;
  // delete all tls form the until chain
  for (auto window_pa = start_pa_entry;
       window_pa - pa_order.begin() < end_pa_entry - pa_order.begin();
       ++window_pa) {
    pa_tls[*window_pa].clear();
  }
  std::string prev_window_guard_constraint_sat = "";
  std::string guard_constraint_sat = "";
  // init curr_to_orig
  for (const auto &pa_tl : orig_tls) {
    for (const auto &pa_entry : pa_tl.second) {
      curr_to_orig[pa_entry.first] = pa_entry.first;
    }
  }
  // encode the until chain from left to right
  for (auto info = infos.begin(); info != infos.end(); ++info) {
    // init round by backing up data from the previous round
    prev_window = curr_window;
    prev_window_guard_constraint_sat = guard_constraint_sat;
    prev_to_orig = curr_to_orig;
    curr_to_orig.clear();
    curr_window.clear();
    // determine context (window begin and end)
    std::pair<int, int> context =
        calculateContext(*info, start_pa, end_pa, lb_acc, ub_acc);
    lb_acc += info->bounds.lower_bound;
    ub_acc = safeAddition(ub_acc, info->bounds.upper_bound);
    std::size_t context_start = context.first;
    std::size_t context_end = context.first + context.second;
    std::string context_pa_start = *(pa_order.begin() + context_start);
    std::string context_pa_end = *(pa_order.begin() + context_end);
    // formulate constraints based on the given bounds
    bool upper_bounded =
        (info->bounds.upper_bound != std::numeric_limits<int>::max());
    bool lower_bounded =
        (info->bounds.lower_bound != 0 || info->bounds.l_op != "&lt;=");
    std::string guard_upper_bound_crossed =
        clock + inverse_op(info->bounds.r_op) +
        std::to_string(info->bounds.upper_bound);
    guard_constraint_sat =
        (lower_bounded ? clock + reverse_op(info->bounds.l_op) +
                             std::to_string(info->bounds.lower_bound)
                       : "") +
        ((lower_bounded && upper_bounded) ? "&amp;&amp;" : "") +
        (upper_bounded ? clock + info->bounds.r_op +
                             std::to_string(info->bounds.upper_bound)
                       : "");
    std::string op_name = info->name + "F" + std::to_string(encode_counter);
    Filter target_filter(info->targets);
    curr_window =
        createWindow(orig_tls, context_pa_start, context_pa_end, base_filter,
                     target_filter, op_name, clock, *info);
    if (info == infos.begin()) {
      // connect TL before start pa to the first window
      if (context_start > 0) {
        std::string prev_pa = *(pa_order.begin() + context_start - 1);
        for (auto &prev_pa_entry : pa_tls[prev_pa]) {
          modifyTransitionsToNextTl(prev_pa_entry.second.trans_out,
                                    prev_pa_entry.first, "", clock + " = 0", "",
                                    op_name);
        }
      }
    }
    OrigMap to_orig;
    curr_to_orig = createOrigMapping(orig_tls, op_name);
    to_orig.insert(prev_to_orig.begin(), prev_to_orig.end());
    to_orig.insert(curr_to_orig.begin(), curr_to_orig.end());

    createTransitionsBetweenWindows(
        s.instances[base_index].first, prev_window, curr_window, to_orig,
        context_pa_start, context_pa_end, prev_window_guard_constraint_sat,
        clock + " = 0");
    // add transitions back to original TLs
    if (info + 1 == infos.end()) {
      std::string last_pa = *(pa_order.begin() + context_end);
      addOutgoingTransOfOrigTL(orig_tls[last_pa], curr_window[last_pa],
                               curr_to_orig, guard_constraint_sat);
    }
    // merge a window once it is done (this is, when outgoing transitions to the
    // next window are added)
    if (info != infos.begin()) {
      mergeWindows(pa_tls, prev_window, true);
    }
  }
  // merge the last window
  mergeWindows(pa_tls, curr_window, true);
}

void DirectEncoder::encodeFuture(AutomataSystem &s, const std::string pa,
                                 const EncICInfo &info, int base_index) {
  int context = calculateContext(info, pa).second;
  Filter target_filter = Filter(info.targets);
  Filter base_filter = Filter(s.instances[base_index].first.states);
  auto search_tl = pa_tls.find(pa);
  if (search_tl != pa_tls.end()) {
    auto search_pa = std::find(pa_order.begin(), pa_order.end(), pa);
    if (search_pa == pa_order.end()) {
      std::cout << "DirectEncoder encodeFuture: could not find pa " << pa
                << std::endl;
      return;
    }
    // formulate clock constraints
    std::string clock = "clX" + info.name;
    bool upper_bounded =
        (info.bounds.upper_bound != std::numeric_limits<int>::max());
    bool lower_bounded =
        (info.bounds.lower_bound != 0 || info.bounds.l_op != "&lt;=");
    std::string guard_upper_bound_crossed =
        clock + inverse_op(info.bounds.r_op) +
        std::to_string(info.bounds.upper_bound);
    std::string guard_constraint_sat =
        (lower_bounded ? clock + reverse_op(info.bounds.l_op) +
                             std::to_string(info.bounds.lower_bound)
                       : "") +
        ((lower_bounded && upper_bounded) ? "&amp;&amp;" : "") +
        (upper_bounded ? clock + info.bounds.r_op +
                             std::to_string(info.bounds.upper_bound)
                       : "");
    // reset clock
    if (search_pa - pa_order.begin() > 0) {
      for (auto &prev_tl_entry :
           pa_tls[pa_order[search_pa - pa_order.begin() - 1]]) {
        base_filter.addToTransitions(
            prev_tl_entry.second.trans_out, "", clock + " = 0",
            pa_order[search_pa - pa_order.begin() - 1], true);
      }
    }
    // determine context (mockup)
    std::size_t context_start = search_pa - pa_order.begin();
    std::size_t context_past_end =
        std::min(search_pa - pa_order.begin() + context + 1,
                 (long int)pa_order.size() - 1);
    TimeLine new_tls;
    std::size_t tls_copied = 0;
    auto curr_future_tl = search_tl;
    encode_counter++;
    // create copies for each tl within the context
    while (Filter::getPrefix(curr_future_tl->first, constants::TL_SEP) !=
           pa_order[context_past_end]) {
      for (auto &tl_entry : curr_future_tl->second) {
        std::string op_name = info.name + "F" + std::to_string(encode_counter);
        std::string new_prefix = addToPrefix(tl_entry.first, op_name);
        Automaton cp_automaton =
            Filter::copyAutomaton(tl_entry.second.ta, new_prefix, false);
        if (upper_bounded) {
          addInvariants(tl_entry.second.ta, base_filter.getFilter(),
                        clock + info.bounds.r_op +
                            std::to_string(info.bounds.upper_bound));
        }
        cp_automaton.clocks.push_back(clock);
        std::vector<Transition> trans_orig_to_cp =
            createCopyTransitionsBetweenTAs(tl_entry.second.ta, cp_automaton,
                                            target_filter.getFilter(),
                                            guard_constraint_sat, "", "");
        std::vector<Transition> cp_to_other_cp;
        std::vector<Transition> cp_to_orig;
        // check if next tl also gets copied
        if (tls_copied + context_start + 1 < context_past_end) {
          cp_to_other_cp =
              addToPrefixOnTransitions(tl_entry.second.trans_out, op_name);
        } else {
          cp_to_other_cp = addToPrefixOnTransitions(tl_entry.second.trans_out,
                                                    op_name, true, false);
          cp_to_orig = createTransitionsBackToOrigTL(
              tl_entry.second.trans_out, new_prefix, tl_entry.first);
          removeTransitionsToNextTl(
              tl_entry.second.trans_out,
              Filter::getPrefix(tl_entry.first, constants::TL_SEP));
        }

        cp_to_other_cp.insert(cp_to_other_cp.end(), trans_orig_to_cp.begin(),
                              trans_orig_to_cp.end());
        cp_to_other_cp.insert(cp_to_other_cp.end(), cp_to_orig.begin(),
                              cp_to_orig.end());
        auto emp = new_tls.emplace(
            std::make_pair(new_prefix, TlEntry(cp_automaton, cp_to_other_cp)));
        if (emp.second == false) {
          std::cout << "DirectEncoder encodeFuture: failed to add prefix: "
                    << new_prefix << std::endl;
        }
      }
      tls_copied++;
      curr_future_tl = pa_tls.find(pa_order[context_start + tls_copied]);
      if (curr_future_tl == pa_tls.end()) {
        std::cout << "DirectEncoder encodeFuture: cannot find next tl"
                  << std::endl;
      }
    }
    std::cout << "DirectEncoder encodeFuture: done with copying timelines, "
              << new_tls.size() << " tls created for activation " << pa
              << std::endl;
    for (const auto &new_tl : new_tls) {
      auto emplaced =
          pa_tls[Filter::getPrefix(new_tl.first, constants::TL_SEP)].emplace(
              new_tl);
      if (emplaced.second == false) {
        std::cout << "DirectEncoder encodeFuture: final merge failed with "
                     "automaton "
                  << new_tl.first << std::endl;
      }
    }
  } else {
    std::cout << "DirectEncoder encodeFuture: could not find timeline of pa "
              << pa << std::endl;
  }
}

void DirectEncoder::encodePast(AutomataSystem &s, std::vector<State> &targets,
                               const std::string pa, const Bounds bounds,
                               int base_index) {
  Filter target_filter = Filter(targets);
  Filter base_filter = Filter(s.instances[base_index].first.states);
  auto search_tl = pa_tls.find(pa);
  if (search_tl != pa_tls.end()) {
    auto search_pa = std::find(pa_order.begin(), pa_order.end(), pa);
    if (search_pa == pa_order.end()) {
      std::cout << "DirectEncoder encodePast: cannot find pa " << pa
                << std::endl;
      return;
    }
    // formulate clock constraints
    std::string clock = "clX" + pa;
    std::string bvar = "bX" + pa;
    bool upper_bounded =
        (bounds.upper_bound != std::numeric_limits<int>::max());
    bool lower_bounded = (bounds.lower_bound != 0 || bounds.l_op != "&lt;=");
    std::string guard_upper_bound_crossed =
        clock + inverse_op(bounds.r_op) + std::to_string(bounds.upper_bound);
    std::string guard_constraint_sat =
        (lower_bounded ? clock + reverse_op(bounds.l_op) +
                             std::to_string(bounds.lower_bound)
                       : "") +
        ((lower_bounded && upper_bounded) ? "&amp;&amp;" : "") +
        (upper_bounded
             ? clock + bounds.r_op + std::to_string(bounds.upper_bound)
             : "");
    // determine context (mockup)
    std::size_t context_end = search_pa - pa_order.begin() - 1;
    std::size_t context_prior_start =
        std::max(search_pa - pa_order.begin() - CONTEXT - 1, (long int)0);
    TimeLine new_tls;
    std::size_t tls_copied = 0;
    auto curr_past_tl = pa_tls.find(pa_order[context_end]);
    if (curr_past_tl == pa_tls.end()) {
      std::cout << "DirectEncoder encodePast: cannot find previous tl"
                << std::endl;
    }
    // create copies for each tl within the context
    encode_counter++;
    while (Filter::getPrefix(curr_past_tl->first, constants::TL_SEP) !=
           pa_order[context_prior_start]) {
      for (auto &tl_entry : curr_past_tl->second) {
        std::string op_name = pa + "P" + std::to_string(encode_counter);
        std::string new_prefix = addToPrefix(tl_entry.first, op_name);
        Automaton cp_automaton =
            Filter::copyAutomaton(tl_entry.second.ta, new_prefix, false);
        if (upper_bounded) {
          addInvariants(cp_automaton, base_filter.getFilter(),
                        clock + bounds.r_op +
                            std::to_string(bounds.upper_bound));
        }
        // target states reached in copy tls
        target_filter.addToTransitions(cp_automaton.transitions, "",
                                       bvar + " = true", "", false);

        cp_automaton.clocks.push_back(clock);
        cp_automaton.bool_vars.push_back(bvar);
        std::vector<Transition> trans_orig_to_cp =
            createCopyTransitionsBetweenTAs(tl_entry.second.ta, cp_automaton,
                                            base_filter.getFilter(), "",
                                            clock + " = 0", "");
        // target states already reached in orig tls
        target_filter.addToTransitions(trans_orig_to_cp, "", bvar + " = true",
                                       "", true);

        std::vector<Transition> cp_to_other_cp;
        std::vector<Transition> cp_to_orig;
        // check if this is the latest tl
        if (tls_copied > 0) {
          cp_to_other_cp =
              addToPrefixOnTransitions(tl_entry.second.trans_out, op_name);
        } else {
          cp_to_other_cp = addToPrefixOnTransitions(tl_entry.second.trans_out,
                                                    op_name, true, false);
          cp_to_orig = createTransitionsBackToOrigTL(
              tl_entry.second.trans_out, new_prefix, tl_entry.first);
          // add constraints that ensure a target state was indeed visited
          // before
          base_filter.addToTransitions(cp_to_orig, bvar + " == true",
                                       bvar + " = false", "", true);

          removeTransitionsToNextTl(tl_entry.second.trans_out, tl_entry.first);
        }
        cp_to_other_cp.insert(cp_to_other_cp.end(), trans_orig_to_cp.begin(),
                              trans_orig_to_cp.end());
        cp_to_other_cp.insert(cp_to_other_cp.end(), cp_to_orig.begin(),
                              cp_to_orig.end());
        new_tls.emplace(
            std::make_pair(new_prefix, TlEntry(cp_automaton, cp_to_other_cp)));
      }
      tls_copied++;
      curr_past_tl = pa_tls.find(pa_order[context_end - tls_copied]);
      if (curr_past_tl == pa_tls.end()) {
        std::cout << "DirectEncoder encodePast: cannot find next tl"
                  << std::endl;
      }
    }
    std::cout << "DirectEncoder encodePast: done with copying timelines, "
              << new_tls.size() << " tls created for activation " << pa
              << std::endl;
    for (const auto &new_tl : new_tls) {
      auto emplaced =
          pa_tls[Filter::getPrefix(new_tl.first, constants::TL_SEP)].emplace(
              new_tl);
      if (emplaced.second == false) {
        std::cout
            << "DirectEncoder encodePast: final merge failed with automaton "
            << new_tl.first << std::endl;
      }
    }
  } else {
    std::cout << "DirectEncoder encodePast: could not find timeline of pa "
              << pa << std::endl;
  }
}

size_t DirectEncoder::getPlanTAIndex() { return plan_ta_index; }

AutomataSystem DirectEncoder::createFinalSystem(const AutomataSystem &s,
                                                SystemVisInfo &s_vis) {
  s_vis = SystemVisInfo(pa_tls, s.instances[plan_ta_index].first.states);
  AutomataSystem res = s;
  res.instances.clear();
  std::vector<Automaton> automata;
  std::vector<Transition> interconnections;
  for (const auto &curr_tl : pa_tls) {
    for (const auto &curr_copy : curr_tl.second) {
      automata.push_back(curr_copy.second.ta);
      interconnections.insert(interconnections.end(),
                              curr_copy.second.trans_out.begin(),
                              curr_copy.second.trans_out.end());
    }
  }
  res.instances.push_back(
      std::make_pair(mergeAutomata(automata, interconnections, "direct"), ""));
  return res;
}

DirectEncoder::DirectEncoder(AutomataSystem &s,
                             const std::vector<PlanAction> &plan,
                             const int base_pos) {
  Automaton plan_ta = generatePlanAutomaton(plan, constants::PLAN_TA_NAME);
  this->plan = plan;
  s.instances.push_back(std::make_pair(plan_ta, ""));
  plan_ta_index = s.instances.size() - 1;
  generateBaseTimeLine(s, base_pos, plan_ta_index);
}
