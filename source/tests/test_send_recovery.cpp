#include "SendRecovery.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace send_recovery;
namespace {
void initialAndEpochChecks() {
  Recovery recovery;assert(recovery.clear()&&recovery.phase()==Phase::Clear);
  assert(!recovery.requestReview(1)&&!recovery.acknowledge(1));
  recovery.markUnknown(17);
  assert(recovery.phase()==Phase::Uncertain&&!recovery.clear()&&recovery.reviewTicket()==0);
  assert(!recovery.observeHistory(17,1,true)&&!recovery.observeResume(17,1,false));
  assert(!recovery.requestReview(16)&&!recovery.requestReview(0));
  assert(recovery.phase()==Phase::Uncertain&&!recovery.acknowledge(17));
}
void bothProofsInEitherOrder() {
  for(bool historyFirst:{false,true}) {
    Recovery recovery;recovery.markUnknown(7);const Ticket ticket=recovery.requestReview(7);
    assert(ticket&&recovery.phase()==Phase::Synchronizing);
    if(historyFirst)assert(recovery.observeHistory(7,ticket,true));
    else assert(recovery.observeResume(7,ticket,false));
    assert(recovery.phase()==Phase::Synchronizing&&!recovery.acknowledge(7));
    if(historyFirst)assert(recovery.observeResume(7,ticket,false));
    else assert(recovery.observeHistory(7,ticket,true));
    assert(recovery.phase()==Phase::Reviewable&&!recovery.clear());
    assert(!recovery.acknowledge(8)&&recovery.acknowledge(7));
    assert(recovery.clear()&&!recovery.acknowledge(7));
    assert(!recovery.observeHistory(7,ticket,true)&&!recovery.observeResume(7,ticket,false));
  }
}
void oldHistoryAndOldEpochNeverUnlock() {
  Recovery recovery;recovery.markUnknown(3);const auto ticket=recovery.requestReview(3);
  assert(!recovery.observeHistory(3,ticket,false));
  assert(!recovery.observeHistory(2,ticket,true)&&!recovery.observeResume(2,ticket,false));
  assert(!recovery.observeHistory(3,0,true)&&!recovery.observeResume(3,ticket+1,false));
  recovery.observeResume(3,ticket,false);
  assert(recovery.phase()==Phase::Synchronizing&&!recovery.acknowledge(3));
  recovery.observeHistory(3,ticket,true);assert(recovery.phase()==Phase::Reviewable);
}
void repeatedRefreshRequiresNewEvidence() {
  Recovery recovery;recovery.markUnknown(9);
  const auto first=recovery.requestReview(9);recovery.observeHistory(9,first,true);
  const auto second=recovery.requestReview(9);assert(first!=second);
  assert(!recovery.observeResume(9,first,false)&&!recovery.observeHistory(9,first,true));
  recovery.observeResume(9,second,false);assert(!recovery.acknowledge(9));
  recovery.observeHistory(9,second,true);assert(recovery.phase()==Phase::Reviewable);
  const auto third=recovery.requestReview(9);assert(third!=second);
  assert(recovery.phase()==Phase::Synchronizing&&!recovery.acknowledge(9));
  assert(!recovery.observeHistory(9,second,true)&&!recovery.observeResume(9,second,false));
  recovery.observeHistory(9,third,true);recovery.observeResume(9,third,false);
  assert(recovery.acknowledge(9));
}
void workingRevokesEarlierReview() {
  Recovery recovery;recovery.markUnknown(4);const auto ticket=recovery.requestReview(4);
  recovery.observeHistory(4,ticket,true);recovery.observeResume(4,ticket,false);
  assert(recovery.phase()==Phase::Reviewable);
  recovery.observeResume(4,ticket,true);
  assert(recovery.phase()==Phase::Synchronizing&&!recovery.acknowledge(4));
  recovery.observeResume(4,ticket,false);assert(!recovery.acknowledge(4));
  recovery.observeHistory(4,ticket,true);assert(recovery.acknowledge(4));
}
void scopeChangesPreserveUncertainty() {
  Recovery recovery;recovery.scopeChanged(5);assert(recovery.clear());
  recovery.markUnknown(5);const auto old=recovery.requestReview(5);
  recovery.observeHistory(5,old,true);recovery.observeResume(5,old,false);
  recovery.scopeChanged(5);assert(recovery.phase()==Phase::Reviewable);
  recovery.scopeChanged(6);assert(recovery.phase()==Phase::Uncertain&&!recovery.clear());
  assert(!recovery.observeHistory(5,old,true)&&!recovery.observeResume(6,old,false)&&!recovery.acknowledge(6));
  const auto current=recovery.requestReview(6);assert(current&&current!=old);
  recovery.observeHistory(6,current,true);recovery.observeResume(6,current,false);
  assert(recovery.acknowledge(6));
}
void aNewAmbiguousSendInvalidatesOldReview() {
  Recovery recovery;recovery.markUnknown(2);const auto old=recovery.requestReview(2);
  recovery.observeHistory(2,old,true);recovery.observeResume(2,old,false);
  recovery.markUnknown(2);assert(recovery.phase()==Phase::Uncertain&&!recovery.acknowledge(2));
  assert(!recovery.observeHistory(2,old,true));
  const auto current=recovery.requestReview(2);assert(current!=old);
}
void zeroEpochFailsClosed() {
  Recovery recovery;recovery.markUnknown(0);assert(!recovery.clear());
  assert(!recovery.requestReview(0)&&!recovery.acknowledge(0));
  recovery.scopeChanged(1);assert(recovery.phase()==Phase::Uncertain);
  assert(recovery.requestReview(1)!=0);
}
void correlatedSettlementDoesNotReuseReviewTickets() {
  Recovery recovery;recovery.markUnknown(11);const auto old=recovery.requestReview(11);
  recovery.observeHistory(11,old,true);recovery.observeResume(11,old,false);
  recovery.observeSettled(10);assert(recovery.phase()==Phase::Reviewable);
  recovery.observeSettled(0);assert(recovery.phase()==Phase::Reviewable);
  recovery.observeSettled(11);assert(recovery.clear()&&recovery.reviewTicket()==0);
  assert(!recovery.observeHistory(11,old,true)&&!recovery.observeResume(11,old,false));
  recovery.markUnknown(11);const auto current=recovery.requestReview(11);
  assert(current>old&&!recovery.clear());
  assert(!recovery.observeHistory(11,old,true)&&!recovery.observeResume(11,old,false));
  recovery.observeResume(11,current,false);assert(!recovery.acknowledge(11));
  recovery.observeHistory(11,current,true);assert(recovery.acknowledge(11));
  recovery.markUnknown(11);const auto third=recovery.requestReview(11);assert(third>current);
}
}
int main() {
  initialAndEpochChecks();bothProofsInEitherOrder();oldHistoryAndOldEpochNeverUnlock();
  repeatedRefreshRequiresNewEvidence();workingRevokesEarlierReview();scopeChangesPreserveUncertainty();
  aNewAmbiguousSendInvalidatesOldReview();zeroEpochFailsClosed();correlatedSettlementDoesNotReuseReviewTickets();
  static_assert(sizeof(Recovery)<=32,"Send recovery state must remain bounded");
  std::printf("Send recovery: 9 API scenario groups passed; review never acknowledges delivery or resends; state=%zu bytes\n",sizeof(Recovery));
}
