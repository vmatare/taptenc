#pragma once
#include "encoder.h"
#include "timed_automata.h"
#include "vis_info.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace taptenc {
class DirectEncoder : Encoder {
private:
  size_t encode_counter = 0;
  size_t plan_ta_index;
  ::std::vector<PlanAction> plan;
  ::std::unordered_map<
      ::std::string,
      ::std::unordered_map<::std::string,
                           ::std::pair<Automaton, ::std::vector<Transition>>>>
      pa_tls;
  ::std::vector<::std::string> pa_order;
  void generateBaseTimeLine(AutomataSystem &s, const int base_pos,
                            const int plan_pos);
  ::std::vector<Transition>
  createTransitionsBackToOrigTL(::std::vector<Transition> &trans,
                                ::std::string prefix, ::std::string pa,
                                ::std::string guard = "");
  ::std::vector<Transition>
  addToPrefixOnTransitions(const ::std::vector<Transition> &trans,
                           ::std::string prefix, bool only_inner_trans = false);
  void modifyTransitionsToNextTl(::std::vector<Transition> &trans,
                                 ::std::string curr_pa, ::std::string guard,
                                 ::std::string update, ::std::string sync);
  void removeTransitionsToNextTl(::std::vector<Transition> &trans,
                                 ::std::string curr_pa);
  ::std::pair<int, int> calculateContext(const EncICInfo &info,
                                         ::std::string starting_pa,
                                         ::std::string ending_pa = "",
                                         int lb_offset = 0, int ub_offset = 0);

public:
  size_t getPlanTAIndex();
  void encodeUntilChain(AutomataSystem &s, const ::std::vector<EncICInfo> &info,
                        const ::std::string start_pa,
                        const ::std::string end_pa, const int base_pos = 0);
  void encodeInvariant(AutomataSystem &s, const ::std::vector<State> &targets,
                       const ::std::string pa);
  void encodeNoOp(AutomataSystem &s, const ::std::vector<State> &targets,
                  const ::std::string pa);
  void encodeFuture(AutomataSystem &s, const ::std::string pa,
                    const EncICInfo &info, int base_index = 0);
  void encodePast(AutomataSystem &s, ::std::vector<State> &targets,
                  const ::std::string pa, const Bounds bounds,
                  int base_index = 0);
  DirectEncoder(AutomataSystem &s, const ::std::vector<PlanAction> &plan,
                const int base_pos = 0);
  AutomataSystem createFinalSystem(const AutomataSystem &s,
                                   SystemVisInfo &s_vis);
};
} // end namespace taptenc
