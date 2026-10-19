#include "OfficialRunGuard.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using official_run_guard::Decision;
using official_run_guard::Guard;

int main() {
  {
    Guard guard;
    assert(!guard.startPending(nullptr)); assert(!guard.startPending(""));
    assert(guard.startPending("request-1")); assert(guard.hasPending());
    assert(guard.hadUnconfirmed());
    assert(!guard.startPending("request-1")); assert(!guard.startPending("request-2"));
    assert(std::strcmp(guard.pendingQueue(),"request-1")==0);
    assert(guard.acceptStarted("run-1","request-other")==Decision::Reject);
    assert(!guard.hasActive()&&guard.hasPending());
    assert(guard.acceptStarted("run-1","request-1")==Decision::Accept);
    assert(!guard.hasPending()&&guard.hasActive()&&!guard.hadUnconfirmed());
    assert(std::strcmp(guard.activeQueue(),"request-1")==0);
    assert(!guard.startPending("request-2"));
    assert(guard.acceptStarted("run-1","request-1")==Decision::Accept);
    assert(guard.acceptStarted("other-run","request-1")==Decision::Reject);
    assert(guard.acceptTerminal("run-1","other-queue")==Decision::Reject);
    assert(guard.acceptTerminal("other-run","request-1")==Decision::Reject);
    assert(guard.hasActive());
    assert(guard.acceptTerminal("run-1","request-1")==Decision::Accept);
    assert(!guard.hasActive()&&!guard.hasPending()&&!guard.hadUnconfirmed());
    assert(!guard.disconnect());
  }
  {
    Guard guard; assert(guard.startPending("coding-request"));
    // Real official coding start/terminal events omit queue_id.
    assert(guard.acceptStarted("response-1")==Decision::Accept);
    assert(guard.hasActive()&&!guard.hasPending()&&guard.hadUnconfirmed());
    assert(guard.acceptTerminal("response-1")==Decision::Accept);
    assert(!guard.hasActive()&&guard.hadUnconfirmed());
    assert(guard.disconnect()&&guard.hadUnconfirmed());
    assert(!guard.hasPending()&&!guard.hasActive());
  }
  {
    Guard correlated;assert(correlated.startPending("qid"));
    assert(correlated.acceptStarted("run","qid")==Decision::Accept);
    assert(!correlated.disconnect()&&!correlated.hasActive()&&!correlated.hadUnconfirmed());
    Guard coding;assert(coding.startPending("qid"));
    assert(coding.acceptStarted("run")==Decision::Accept);
    assert(coding.disconnect()&&!coding.hasActive()&&coding.hadUnconfirmed());
  }
  {
    Guard guard; assert(guard.startPending("qid"));
    assert(guard.acceptStarted("response")==Decision::Accept&&guard.hadUnconfirmed());
    assert(guard.acceptStarted("response","qid")==Decision::Accept&&!guard.hadUnconfirmed());
    assert(guard.acceptTerminal("response")==Decision::Accept&&!guard.hadUnconfirmed());
  }
  {
    Guard guard; assert(guard.startPending("qid"));
    // A definite validation failure has queue_id but no run_id.
    assert(guard.acceptTerminal(nullptr,"wrong")==Decision::Reject);
    assert(guard.acceptTerminal(nullptr,"qid")==Decision::Accept);
    assert(!guard.hasPending()&&!guard.hadUnconfirmed());
  }
  {
    Guard guard; assert(guard.startPending("qid"));
    assert(guard.acceptTerminal()==Decision::Reconcile);
    assert(guard.hasPending()&&guard.hadUnconfirmed());
    assert(guard.acceptTerminal("unbound-run")==Decision::Reconcile&&guard.hasPending());
    assert(guard.acceptStarted(nullptr,"qid")==Decision::Reconcile&&guard.hasPending());
    assert(guard.disconnect()&&!guard.hasPending()&&guard.hadUnconfirmed());
  }
  {
    Guard guard; assert(guard.startPending("qid"));
    assert(guard.acceptStarted("response","qid")==Decision::Accept);
    assert(guard.acceptTerminal()==Decision::Reconcile&&guard.hasActive());
    assert(guard.acceptAbort()==Decision::Reconcile&&guard.hasActive());
    assert(guard.acceptAbort("managed-process-id")==Decision::Reconcile&&guard.hasActive());
    assert(guard.acceptAbort("response","wrong-queue")==Decision::Reject&&guard.hasActive());
    assert(guard.acceptAbort("response","qid")==Decision::Accept&&!guard.hasActive());
  }
  {
    Guard guard;
    assert(guard.acceptStarted("server-run","server-queue")==Decision::Accept);
    assert(guard.hasActive()&&!guard.hadUnconfirmed());
    assert(!guard.startPending("new-input"));
    assert(guard.acceptTerminal("server-run","other-queue")==Decision::Reject);
    assert(guard.acceptTerminal("server-run")==Decision::Accept);
    assert(!guard.disconnect());
    assert(guard.acceptTerminal("unbound-run")==Decision::Reconcile);
  }
  {
    // No-ID server events cannot erase a previous local uncertain result.
    Guard guard;assert(guard.startPending("uncertain"));assert(guard.disconnect());
    assert(guard.acceptStarted("unrelated-server-run")==Decision::Accept);
    assert(guard.acceptTerminal("unrelated-server-run")==Decision::Accept);
    assert(guard.hadUnconfirmed());
  }
  {
    const std::string q64(64,'q'), q65(65,'q'), r128(128,'r'), r129(129,'r');
    Guard guard;assert(!guard.startPending(q65.c_str()));assert(guard.startPending(q64.c_str()));
    assert(guard.acceptStarted(r129.c_str(),q64.c_str())==Decision::Reject&&guard.hasPending());
    assert(guard.acceptStarted(r128.c_str(),q64.c_str())==Decision::Accept);
    assert(guard.acceptTerminal(r129.c_str(),q64.c_str())==Decision::Reject&&guard.hasActive());
    assert(guard.acceptTerminal(r128.c_str(),q64.c_str())==Decision::Accept);
    Guard observed;assert(observed.acceptStarted(r128.c_str(),r128.c_str())==Decision::Accept);
    assert(observed.acceptTerminal(r128.c_str(),r129.c_str())==Decision::Reject&&observed.hasActive());
    assert(observed.acceptTerminal(r128.c_str(),r128.c_str())==Decision::Accept);
    Guard invalid;assert(!invalid.startPending("qid\r\nX"));
    assert(invalid.acceptStarted("run\x7f")==Decision::Reject);
  }
  {
    // Own storage: caller mutation does not change correlation identities.
    char queue[]="queue-owned", run[]="run-owned";
    Guard guard;assert(guard.startPending(queue));queue[0]='X';
    assert(guard.acceptStarted(run,"queue-owned")==Decision::Accept);run[0]='Y';
    assert(guard.acceptTerminal("run-owned","queue-owned")==Decision::Accept);
  }
  static_assert(sizeof(Guard)<512,"run guard must stay small and allocation-free");
  std::cout<<"official run guard tests passed; state "<<sizeof(Guard)<<" bytes\n";
}
