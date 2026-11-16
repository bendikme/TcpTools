#pragma once
#include "pch.h"

// ============================================================
//  NetAddr - addresses for IPv4 and IPv6, shared by the tools
//
//    text      SplitHostPort / JoinHostPort / IsValidHost / ToString
//    listen    CreateListenSocket   dual-stack (IPv4 + IPv6), exclusive port
//    resolve   Resolve              name lookup that can be cancelled
//    connect   Connect              first endpoint that answers, cancellable
//
//  Everything here works on any thread and keeps no state.
// ============================================================
namespace NetAddr
{
    // One socket address of either family.
    struct Endpoint {
        sockaddr_storage addr {};
        int              len  {0};

        const sockaddr* sa() const     { return reinterpret_cast<const sockaddr*>(&addr); }
        sockaddr*       sa()           { return reinterpret_cast<sockaddr*>(&addr); }
        int             family() const { return addr.ss_family; }
        bool            valid() const  { return len > 0; }
    };

    // ---- Text ------------------------------------------------------------

    inline bool ParsePort(const std::wstring& s, int& port)
    {
        if (s.empty() || s.size() > 5) return false;
        int v = 0;
        for (wchar_t c : s) {
            if (c < L'0' || c > L'9') return false;
            v = v * 10 + (c - L'0');
        }
        if (v < 1 || v > 65535) return false;
        port = v;
        return true;
    }

    struct HostPort {
        std::wstring host;              // without brackets
        int          port    {0};
        bool         hasPort {false};
    };

    // Splits what a person types into host and port:
    //    example.com        example.com:502        10.0.0.5:502
    //    ::1                fe80::1%12             (a bare IPv6 address: several colons, never a port)
    //    [::1]              [::1]:502
    // False when the text is none of these ("", "[::1", "host:", "host:abc", ":80").
    inline bool SplitHostPort(std::wstring text, HostPort& out)
    {
        out = {};
        const size_t a = text.find_first_not_of(L" \t");
        if (a == std::wstring::npos) return false;
        text = text.substr(a, text.find_last_not_of(L" \t") - a + 1);

        if (text[0] == L'[') {
            const size_t close = text.find(L']');
            if (close == std::wstring::npos || close == 1) return false;
            out.host = text.substr(1, close - 1);
            const std::wstring rest = text.substr(close + 1);
            if (rest.empty()) return true;
            if (rest[0] != L':' || !ParsePort(rest.substr(1), out.port)) return false;
            out.hasPort = true;
            return true;
        }

        const size_t first = text.find(L':'), last = text.rfind(L':');
        if (first == std::wstring::npos || first != last) {     // no colon, or a bare IPv6 address
            out.host = text;
            return true;
        }
        if (first == 0 || !ParsePort(text.substr(first + 1), out.port)) return false;
        out.host    = text.substr(0, first);
        out.hasPort = true;
        return true;
    }

    // True for an IPv6 address in text form (no brackets), with or without a "%zone".
    inline bool IsIPv6Literal(const std::wstring& host)
    {
        if (host.find(L':') == std::wstring::npos) return false;
        std::wstring addr = host.substr(0, host.find(L'%'));
        in6_addr tmp;
        return InetPtonW(AF_INET6, addr.c_str(), &tmp) == 1;
    }

    // "host:port" for a name or IPv4 address, "[host]:port" for an IPv6 address.
    inline std::wstring JoinHostPort(const std::wstring& host, int port)
    {
        const bool v6 = host.find(L':') != std::wstring::npos;
        return (v6 ? L"[" + host + L"]" : host) + L":" + std::to_wstring(port);
    }

    // A host as it may be typed: a DNS name, an IPv4 address, or an IPv6 address
    // (bare or in brackets). Says nothing about whether it exists.
    inline bool IsValidHost(std::wstring host)
    {
        if (host.size() >= 2 && host.front() == L'[' && host.back() == L']')
            host = host.substr(1, host.size() - 2);
        if (host.empty() || host.size() > 253) return false;
        if (host.find(L':') != std::wstring::npos) return IsIPv6Literal(host);
        for (wchar_t c : host) {
            // letters of any script (international names), digits, '-', '.', '_'
            if (c <= 0x20 || c == 0x7F) return false;
            if (c < 0x80 && !iswalnum(c) && c != L'-' && c != L'.' && c != L'_') return false;
        }
        return host.front() != L'.' && host.front() != L'-' && host.back() != L'-';
    }

    // "10.0.0.5:502", "[fe80::1%12]:502"; an IPv4 peer of a dual-stack socket
    // (::ffff:10.0.0.5) is shown as the IPv4 address it is.
    inline std::wstring ToString(const sockaddr* sa, bool withPort = true)
    {
        if (!sa) return L"?";
        wchar_t buf[INET6_ADDRSTRLEN] = {};
        if (sa->sa_family == AF_INET) {
            auto* a = reinterpret_cast<const sockaddr_in*>(sa);
            InetNtopW(AF_INET, &a->sin_addr, buf, INET6_ADDRSTRLEN);
            return withPort ? std::wstring(buf) + L":" + std::to_wstring(ntohs(a->sin_port)) : std::wstring(buf);
        }
        if (sa->sa_family == AF_INET6) {
            auto* a = reinterpret_cast<const sockaddr_in6*>(sa);
            if (IN6_IS_ADDR_V4MAPPED(&a->sin6_addr)) {
                in_addr v4;
                memcpy(&v4, a->sin6_addr.u.Byte + 12, 4);
                InetNtopW(AF_INET, &v4, buf, INET6_ADDRSTRLEN);
                return withPort ? std::wstring(buf) + L":" + std::to_wstring(ntohs(a->sin6_port)) : std::wstring(buf);
            }
            InetNtopW(AF_INET6, &a->sin6_addr, buf, INET6_ADDRSTRLEN);
            std::wstring s = buf;
            if (a->sin6_scope_id != 0) s += L"%" + std::to_wstring(a->sin6_scope_id);
            return withPort ? L"[" + s + L"]:" + std::to_wstring(ntohs(a->sin6_port)) : s;
        }
        return L"?";
    }
    inline std::wstring ToString(const Endpoint& e, bool withPort = true) { return ToString(e.sa(), withPort); }

    // Same address, port (and zone)? Used to tell UDP senders apart.
    inline bool Same(const Endpoint& x, const Endpoint& y)
    {
        if (x.family() != y.family()) return false;
        if (x.family() == AF_INET) {
            auto* a = reinterpret_cast<const sockaddr_in*>(&x.addr);
            auto* b = reinterpret_cast<const sockaddr_in*>(&y.addr);
            return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
        }
        if (x.family() == AF_INET6) {
            auto* a = reinterpret_cast<const sockaddr_in6*>(&x.addr);
            auto* b = reinterpret_cast<const sockaddr_in6*>(&y.addr);
            return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id &&
                   memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(in6_addr)) == 0;
        }
        return false;
    }

    // The local / remote address of a socket ("" when it has none).
    inline std::wstring LocalName(SOCKET s, bool withPort = true)
    {
        Endpoint e; e.len = sizeof(e.addr);
        return getsockname(s, e.sa(), &e.len) == 0 ? ToString(e, withPort) : std::wstring();
    }
    inline std::wstring PeerName(SOCKET s, bool withPort = true)
    {
        Endpoint e; e.len = sizeof(e.addr);
        return getpeername(s, e.sa(), &e.len) == 0 ? ToString(e, withPort) : std::wstring();
    }

    // Readable text of a Winsock error: "No connection could be made ... (10061)".
    inline std::wstring ErrorText(int wsaErr)
    {
        wchar_t buf[256] = {};
        DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, (DWORD)wsaErr, 0, buf, 256, nullptr);
        while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L' ' || buf[n - 1] == L'.')) buf[--n] = 0;
        std::wstring s = n ? buf : L"Error";
        return s + L" (" + std::to_wstring(wsaErr) + L")";
    }

    // ---- Listen ----------------------------------------------------------

    // A socket bound to `port` on every local address, IPv4 and IPv6 (one
    // dual-stack socket; plain IPv4 when the machine has no IPv6). The port is
    // taken exclusively: a second bind - here or in another program - fails
    // with WSAEADDRINUSE / WSAEACCES instead of silently sharing the port.
    // For TCP the caller still calls listen(). INVALID_SOCKET + err on failure.
    inline SOCKET CreateListenSocket(int socktype, int port, int& err)
    {
        err = 0;
        const int proto = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
        const BOOL exclusive = TRUE;

        SOCKET s = socket(AF_INET6, socktype, proto);
        if (s != INVALID_SOCKET) {
            DWORD v6only = 0;
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));
            setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
            sockaddr_in6 a = {};
            a.sin6_family = AF_INET6;
            a.sin6_port   = htons((u_short)port);
            a.sin6_addr   = in6addr_any;
            if (bind(s, (sockaddr*)&a, sizeof(a)) == 0) return s;
            err = WSAGetLastError();
            closesocket(s);
            // The port is taken: an IPv4-only socket must not sneak in beside it.
            if (err == WSAEADDRINUSE || err == WSAEACCES) return INVALID_SOCKET;
        }

        s = socket(AF_INET, socktype, proto);                 // no IPv6 on this machine
        if (s == INVALID_SOCKET) { err = WSAGetLastError(); return INVALID_SOCKET; }
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
        sockaddr_in a4 = {};
        a4.sin_family      = AF_INET;
        a4.sin_port        = htons((u_short)port);
        a4.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (sockaddr*)&a4, sizeof(a4)) == 0) { err = 0; return s; }
        err = WSAGetLastError();
        closesocket(s);
        return INVALID_SOCKET;
    }

    // Could a server take this port right now?
    inline bool IsPortFree(int port, bool udp)
    {
        int err = 0;
        SOCKET s = CreateListenSocket(udp ? SOCK_DGRAM : SOCK_STREAM, port, err);
        if (s == INVALID_SOCKET) return false;
        closesocket(s);
        return true;
    }

    // ---- Resolve ---------------------------------------------------------

    // Looks `host` up (name, IPv4 or IPv6 text; no brackets) and returns its
    // addresses, IPv4 and IPv6, in the order the system prefers them.
    // The lookup runs asynchronously and `cancel` is polled every 50 ms, so the
    // caller - a worker thread - gets away from a slow DNS server at once.
    // Returns 0, WSA_E_CANCELLED, or the Winsock error of the lookup.
    inline int Resolve(const std::wstring& host, int port, int socktype,
                       const std::atomic<bool>& cancel, std::vector<Endpoint>& out)
    {
        out.clear();
        ADDRINFOEXW hints = {};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = socktype;
        hints.ai_protocol = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

        const std::wstring service = std::to_wstring(port);
        PADDRINFOEXW res = nullptr;
        OVERLAPPED   ov  = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return WSA_NOT_ENOUGH_MEMORY;
        HANDLE hCancel = nullptr;

        int rc = GetAddrInfoExW(host.c_str(), service.c_str(), NS_DNS, nullptr, &hints,
                                &res, nullptr, &ov, nullptr, &hCancel);
        if (rc == WSA_IO_PENDING) {
            bool cancelled = false;
            while (WaitForSingleObject(ov.hEvent, 50) != WAIT_OBJECT_0) {
                if (!cancelled && cancel.load()) {
                    cancelled = true;
                    GetAddrInfoExCancel(&hCancel);      // the event is still signalled afterwards
                }
            }
            rc = GetAddrInfoExOverlappedResult(&ov);
        }
        CloseHandle(ov.hEvent);

        if (rc == 0 && !cancel.load()) {
            for (PADDRINFOEXW p = res; p; p = p->ai_next) {
                if (!p->ai_addr || p->ai_addrlen > sizeof(sockaddr_storage)) continue;
                if (p->ai_family != AF_INET && p->ai_family != AF_INET6) continue;
                Endpoint e;
                memcpy(&e.addr, p->ai_addr, p->ai_addrlen);
                e.len = (int)p->ai_addrlen;
                out.push_back(e);
            }
        }
        if (res) FreeAddrInfoExW(res);

        if (cancel.load()) { out.clear(); return WSA_E_CANCELLED; }
        if (rc != 0) return rc;
        return out.empty() ? WSAHOST_NOT_FOUND : 0;
    }

    // ---- Connect ---------------------------------------------------------

    // Connects a new socket to the first of `endpoints` that answers and returns
    // it (still non-blocking when `keepNonBlocking`, else blocking).
    //   timeoutMs   per endpoint; 0 = no limit
    //   cancel      polled every 50 ms; a cancelled connect returns
    //               INVALID_SOCKET with err = WSA_E_CANCELLED
    // On failure err is the error of the last endpoint tried (WSAETIMEDOUT for a
    // timeout). For UDP the "connection" only fixes the peer address.
    inline SOCKET Connect(const std::vector<Endpoint>& endpoints, int socktype, int timeoutMs,
                          const std::atomic<bool>& cancel, int& err,
                          bool keepNonBlocking = false, Endpoint* connectedTo = nullptr)
    {
        err = WSAHOST_NOT_FOUND;
        const int proto = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

        for (const Endpoint& ep : endpoints) {
            if (cancel.load()) { err = WSA_E_CANCELLED; return INVALID_SOCKET; }

            SOCKET s = socket(ep.family(), socktype, proto);
            if (s == INVALID_SOCKET) { err = WSAGetLastError(); continue; }
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);

            bool ok = false;
            if (::connect(s, ep.sa(), ep.len) == 0) {
                ok = true;
            } else if ((err = WSAGetLastError()) == WSAEWOULDBLOCK) {
                const ULONGLONG start = GetTickCount64();
                for (;;) {
                    if (cancel.load()) { closesocket(s); err = WSA_E_CANCELLED; return INVALID_SOCKET; }
                    fd_set wfds, efds;
                    FD_ZERO(&wfds); FD_SET(s, &wfds);
                    FD_ZERO(&efds); FD_SET(s, &efds);
                    timeval tv { 0, 50000 };
                    int r = select(0, nullptr, &wfds, &efds, &tv);
                    if (r == SOCKET_ERROR) { err = WSAGetLastError(); break; }
                    if (r > 0) {
                        if (FD_ISSET(s, &efds)) {
                            int soErr = 0, len = sizeof(soErr);
                            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
                            err = soErr ? soErr : WSAECONNREFUSED;
                        } else {
                            ok = true;
                        }
                        break;
                    }
                    if (timeoutMs > 0 && GetTickCount64() - start >= (ULONGLONG)timeoutMs) { err = WSAETIMEDOUT; break; }
                }
            }

            if (ok) {
                if (!keepNonBlocking) { nb = 0; ioctlsocket(s, FIONBIO, &nb); }
                if (connectedTo) *connectedTo = ep;
                err = 0;
                return s;
            }
            closesocket(s);
        }
        return INVALID_SOCKET;
    }
} // namespace NetAddr
