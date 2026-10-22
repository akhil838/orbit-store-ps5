// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/native_provider.hpp"
#include <utility>

namespace orbit {
void NativeProvider::reset() {
    client_.clear(); id_.clear();
    launched_ = closing_ = cancelled_ = running_ = false;
    close_attempts_ = 0;
    next_close_ = 0;
}
void NativeProvider::begin(std::string client, double now) {
    if (busy()) return;
    reset(); client_ = std::move(client); began_ = last_seen_ = now;
}
NativeProvider::Event NativeProvider::tick(const BrowserSession &s, bool fresh, double now) {
    Event event;
    if (!window_ || !busy()) return event;
    const bool ours = s.client_id == client_ && !s.id.empty() && (id_.empty() || s.id == id_);
    const bool committed = ours && s.state == "queued" && !s.job_id.empty();
    const bool terminal = ours && (committed || s.state == "failed" || s.state == "cancelled");
    if (fresh && ours) last_seen_ = now;
    const bool expired = now - began_ > 240 || (launched_ && now - last_seen_ > 12);
    if (!launched_) {
        if (terminal || now - began_ > 35) { reset(); return event; }
        if (!fresh || !ours || !s.active || s.dialog_opened || s.page_url.empty()) return event;
        id_ = s.id;
        // The service supplies only its selected catalogue page. HTTPS also
        // prevents accidentally opening an internal/action URL on bad input.
        if (s.page_url.rfind("https://", 0) != 0 || !window_->open(s.page_url)) {
            event = {Event::cancel, id_, client_, "The native download page could not open. Return to Orbit and try again."};
            reset(); return event;
        }
        launched_ = true; opened_at_ = now;
        return {Event::opened, id_, client_, {}};
    }
    const int status = window_->status();
    if (status == 2) running_ = true;
    if (status <= 0 || status == 3 || (status == 1 && (running_ || now - opened_at_ > 5))) {
        if (!terminal && !cancelled_) event = {Event::cancel, id_, client_, status < 0 ? "The native browser stopped responding. Return to Orbit and try again." : ""};
        reset(); return event;
    }
    // Close only the dialog this process opened. A new session, backend loss,
    // failure or cancellation must never be mistaken for successful queuing.
    if (terminal || expired || (fresh && !ours) || status < 0) closing_ = true;
    if (closing_ && !terminal && !cancelled_) {
        cancelled_ = true;
        event = {Event::cancel, id_, client_, expired ? "Connection to Orbit was lost. Please return to Orbit and try again." : ""};
    }
    if (closing_ && status == 2 && now >= next_close_ && close_attempts_ < 3) {
        ++close_attempts_; next_close_ = now + 2;
        (void)window_->close();
        // Close is asynchronous: keep the UI blocked until FINISHED, not just
        // because the close request was accepted.
    }
    return event;
}
} // namespace orbit
