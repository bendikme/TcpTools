#pragma once
#include "pch.h"

// ============================================================
//  HandoffQueue - network threads -> window, in order, bounded
//
//  A network thread Push()es its events (connected, data, error, closed ...);
//  the window gets ONE posted message when the queue goes from empty to
//  non-empty and Drain()s everything that has gathered since. So:
//
//    - a megabyte that arrives in 16 chunks is one wake-up for the window,
//      which can show it with one append instead of 16;
//    - the message queue cannot overflow (it holds at most one message per
//      queue), and no event is ever dropped;
//    - events keep their order, because everything goes through the same queue;
//    - when the window falls behind, Push() blocks once `maxBytes` are waiting:
//      the thread stops reading its socket and TCP slows the sender down,
//      instead of memory filling up;
//    - events still queued when the page goes away are destroyed with the
//      queue: Event owns its payload (use std::vector / std::wstring members).
//
//  Event is any movable type. One queue per page; any number of producers.
// ============================================================
template <class Event>
class HandoffQueue
{
public:
    explicit HandoffQueue(size_t maxBytes = 8 * 1024 * 1024) : m_maxBytes(maxBytes) {}

    // The window that drains the queue and the message it is woken with.
    // Call before the first producer starts.
    void SetTarget(HWND hWnd, UINT message)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_hWnd    = hWnd;
        m_message = message;
    }

    // Producer side. `bytes` is the event's weight for the size limit (payload
    // size; 0 for a state change, which therefore never waits). Blocks while
    // the queue is full and `abort` is false. Returns false - and drops the
    // event - only when aborted or closed.
    bool Push(Event&& ev, size_t bytes, const std::atomic<bool>& abort)
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        while (bytes != 0 && m_bytes != 0 && m_bytes + bytes > m_maxBytes && !m_closed && !abort.load()) {
            // Woken by Drain(); the timeout only bounds how long an abort goes unnoticed.
            m_room.wait_for(lk, std::chrono::milliseconds(50));
        }
        if (m_closed || (bytes != 0 && abort.load())) return false;

        m_items.push_back(Item{ std::move(ev), bytes });
        m_bytes += bytes;
        if (!m_posted && m_hWnd) {
            // A failed post (window gone, or its queue full of other things)
            // leaves m_posted false, so the next Push tries again.
            m_posted = PostMessageW(m_hWnd, m_message, 0, 0) != FALSE;
        }
        return true;
    }

    // Window side: takes what is queued, oldest first, up to about `maxBytes` of
    // payload (at least one event). When events remain, the wake-up message is
    // posted again, so the window can handle input between two slices.
    std::vector<Event> Drain(size_t maxBytes = SIZE_MAX)
    {
        std::vector<Event> out;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            size_t taken = 0;
            while (!m_items.empty() && (out.empty() || taken + m_items.front().bytes <= maxBytes)) {
                taken   += m_items.front().bytes;
                m_bytes -= m_items.front().bytes;
                out.push_back(std::move(m_items.front().ev));
                m_items.pop_front();
            }
            m_posted = false;
            if (!m_items.empty() && m_hWnd)
                m_posted = PostMessageW(m_hWnd, m_message, 0, 0) != FALSE;
        }
        m_room.notify_all();
        return out;
    }

    // Throws away what is queued (for example when a run is stopped and its
    // remaining events no longer matter).
    void Clear()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_items.clear();
            m_bytes = 0;
        }
        m_room.notify_all();
    }

    // The window is going away: no more wake-ups, producers stop waiting and
    // every later Push fails. The queue can not be reopened.
    void Close()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_closed = true;
            m_hWnd   = nullptr;
            m_items.clear();
            m_bytes  = 0;
        }
        m_room.notify_all();
    }

    bool Empty()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_items.empty();
    }

private:
    struct Item { Event ev; size_t bytes; };

    std::mutex              m_mutex;
    std::condition_variable m_room;
    std::deque<Item>        m_items;
    size_t                  m_bytes   {0};
    const size_t            m_maxBytes;
    HWND                    m_hWnd    {nullptr};
    UINT                    m_message {0};
    bool                    m_posted  {false};
    bool                    m_closed  {false};
};
