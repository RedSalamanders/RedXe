#include "ZoomAuth.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <wincred.h>
#include <winhttp.h>

#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace Zoom
{
namespace
{
constexpr char kBase64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] bool RandomBytes(uint8_t* bytes, size_t count) noexcept
{
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

[[nodiscard]] bool Sha256(const uint8_t* bytes, size_t count, uint8_t* digest) noexcept
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
    {
        return false;
    }
    const auto closeAlgorithm = wil::scope_exit([&]() noexcept { BCryptCloseAlgorithmProvider(algorithm, 0); });
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        return false;
    }
    const auto destroyHash = wil::scope_exit([&]() noexcept { BCryptDestroyHash(hash); });
    if (!BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(count), 0)))
    {
        return false;
    }
    return BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, 32, 0));
}

[[nodiscard]] bool AppendText(char* text, size_t capacity, size_t& used, std::string_view piece) noexcept
{
    if (used + piece.size() >= capacity)
    {
        return false;
    }
    std::memcpy(text + used, piece.data(), piece.size());
    used += piece.size();
    text[used] = '\0';
    return true;
}

[[nodiscard]] bool AppendUrlEncoded(char* text, size_t capacity, size_t& used, std::string_view piece) noexcept
{
    for (const char character : piece)
    {
        const bool safe = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') || character == '-' || character == '_' ||
                          character == '.' || character == '~';
        if (safe)
        {
            const char one[2]{character, '\0'};
            if (!AppendText(text, capacity, used, std::string_view{one, 1}))
            {
                return false;
            }
        }
        else
        {
            char escaped[4]{};
            (void)sprintf_s(escaped, "%%%02X", static_cast<unsigned>(static_cast<unsigned char>(character)));
            if (!AppendText(text, capacity, used, std::string_view{escaped, 3}))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool Utf8ToWide(std::string_view text, wchar_t* wide, int capacity) noexcept
{
    if (!wide || capacity <= 0)
    {
        return false;
    }
    wide[0] = L'\0';
    if (text.empty())
    {
        return true;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            wide, capacity - 1);
    if (written <= 0)
    {
        return false;
    }
    wide[written] = L'\0';
    return true;
}

[[nodiscard]] bool BuildCredentialTarget(std::string_view clientId, wchar_t* target, int capacity) noexcept
{
    std::array<char, 256> utf8{};
    size_t used = 0;
    if (clientId.empty() || !AppendText(utf8.data(), utf8.size(), used, kCredentialTargetPrefix) ||
        !AppendText(utf8.data(), utf8.size(), used, clientId))
    {
        return false;
    }
    return Utf8ToWide(std::string_view{utf8.data(), used}, target, capacity);
}

[[nodiscard]] HRESULT PostForm(ITokenTransport& transport, std::string_view domain, const char* body, uint64_t nowUnix,
                               TokenSet& tokens) noexcept
{
    std::unique_ptr<char[]> response(new (std::nothrow) char[kMaximumTokenResponseBytes]);
    if (!response)
    {
        return E_OUTOFMEMORY;
    }
    size_t received = 0;
    const HRESULT posted = transport.Post(domain, body, response.get(), kMaximumTokenResponseBytes, received);
    if (FAILED(posted))
    {
        return posted;
    }
    return ParseTokenResponse(std::string_view{response.get(), received}, nowUnix, tokens);
}
} // namespace

uint64_t UnixNow() noexcept
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const uint64_t ticks = (static_cast<uint64_t>(now.dwHighDateTime) << 32U) | now.dwLowDateTime;
    return (ticks - 116444736000000000ULL) / 10000000ULL;
}

bool Base64UrlEncode(const uint8_t* bytes, size_t count, char* text, size_t capacity) noexcept
{
    if (!bytes || !text)
    {
        return false;
    }
    const size_t needed = (count * 4 + 2) / 3;
    if (needed + 1 > capacity)
    {
        return false;
    }
    size_t used = 0;
    for (size_t index = 0; index < count; index += 3)
    {
        const uint32_t first = bytes[index];
        const uint32_t second = index + 1 < count ? bytes[index + 1] : 0;
        const uint32_t third = index + 2 < count ? bytes[index + 2] : 0;
        const uint32_t triple = (first << 16U) | (second << 8U) | third;
        text[used++] = kBase64Url[(triple >> 18U) & 0x3FU];
        text[used++] = kBase64Url[(triple >> 12U) & 0x3FU];
        if (index + 1 < count)
        {
            text[used++] = kBase64Url[(triple >> 6U) & 0x3FU];
        }
        if (index + 2 < count)
        {
            text[used++] = kBase64Url[triple & 0x3FU];
        }
    }
    text[used] = '\0';
    return true;
}

bool GenerateVerifier(char* text, size_t capacity) noexcept
{
    // 48 random bytes become exactly 64 base64url characters, inside the 43–128 range PKCE requires.
    std::array<uint8_t, 48> random{};
    return RandomBytes(random.data(), random.size()) && Base64UrlEncode(random.data(), random.size(), text, capacity);
}

bool GenerateState(char* text, size_t capacity) noexcept
{
    std::array<uint8_t, 16> random{};
    return RandomBytes(random.data(), random.size()) && Base64UrlEncode(random.data(), random.size(), text, capacity);
}

bool ComputeChallenge(std::string_view verifier, char* text, size_t capacity) noexcept
{
    std::array<uint8_t, 32> digest{};
    if (verifier.empty() || verifier.size() > 128 ||
        !Sha256(reinterpret_cast<const uint8_t*>(verifier.data()), verifier.size(), digest.data()))
    {
        return false;
    }
    return Base64UrlEncode(digest.data(), digest.size(), text, capacity);
}

bool BuildEncodedRedirectUri(uint32_t redirectPort, char* text, size_t capacity) noexcept
{
    std::array<char, 64> plain{};
    (void)sprintf_s(plain.data(), plain.size(), "http://127.0.0.1:%u%s", redirectPort, kRedirectPath);
    size_t used = 0;
    if (!text || capacity == 0)
    {
        return false;
    }
    text[0] = '\0';
    return AppendUrlEncoded(text, capacity, used, plain.data());
}

bool BuildAuthorizeUrl(std::string_view domain, std::string_view clientId, uint32_t redirectPort,
                       std::string_view challenge, std::string_view state, char* url, size_t capacity) noexcept
{
    if (!url || capacity == 0 || domain.empty() || clientId.empty() || challenge.empty() || state.empty())
    {
        return false;
    }
    url[0] = '\0';
    size_t used = 0;
    std::array<char, 96> redirect{};
    return BuildEncodedRedirectUri(redirectPort, redirect.data(), redirect.size()) &&
           AppendText(url, capacity, used, "https://") && AppendText(url, capacity, used, domain) &&
           AppendText(url, capacity, used, kAuthorizePath) &&
           AppendText(url, capacity, used, "?response_type=code&client_id=") &&
           AppendUrlEncoded(url, capacity, used, clientId) && AppendText(url, capacity, used, "&redirect_uri=") &&
           AppendText(url, capacity, used, redirect.data()) && AppendText(url, capacity, used, "&code_challenge=") &&
           AppendText(url, capacity, used, challenge) &&
           AppendText(url, capacity, used, "&code_challenge_method=S256&state=") &&
           AppendText(url, capacity, used, state);
}

HRESULT ParseTokenResponse(std::string_view json, uint64_t nowUnix, TokenSet& tokens) noexcept
{
    if (json.empty())
    {
        return E_INVALIDARG;
    }
    yyjson_doc* document = yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG);
    if (!document)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const auto freeDocument = wil::scope_exit([&]() noexcept { yyjson_doc_free(document); });
    yyjson_val* root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_val* error = yyjson_obj_get(root, "error");
    if (yyjson_is_str(error))
    {
        return E_ACCESSDENIED;
    }
    yyjson_val* access = yyjson_obj_get(root, "access_token");
    if (!yyjson_is_str(access) || yyjson_get_len(access) == 0 || yyjson_get_len(access) > kMaximumTokenBytes)
    {
        return E_INVALIDARG;
    }
    yyjson_val* refresh = yyjson_obj_get(root, "refresh_token");
    if (refresh && (!yyjson_is_str(refresh) || yyjson_get_len(refresh) > kMaximumTokenBytes))
    {
        return E_INVALIDARG;
    }
    yyjson_val* expires = yyjson_obj_get(root, "expires_in");
    uint64_t lifetime = 3600;
    if (expires)
    {
        if (!yyjson_is_uint(expires))
        {
            return E_INVALIDARG;
        }
        lifetime = yyjson_get_uint(expires);
    }
    tokens.accessToken.fill('\0');
    std::memcpy(tokens.accessToken.data(), yyjson_get_str(access), yyjson_get_len(access));
    tokens.accessBytes = static_cast<uint32_t>(yyjson_get_len(access));
    if (refresh)
    {
        tokens.refreshToken.fill('\0');
        std::memcpy(tokens.refreshToken.data(), yyjson_get_str(refresh), yyjson_get_len(refresh));
        tokens.refreshBytes = static_cast<uint32_t>(yyjson_get_len(refresh));
    }
    tokens.expiresAt = nowUnix + lifetime;
    return S_OK;
}

HRESULT WinHttpTokenTransport::Post(std::string_view domain, std::string_view body, char* response, size_t capacity,
                                    size_t& received) noexcept
{
    received = 0;
    if (!response || capacity == 0 || domain.empty() || domain.size() > 255)
    {
        return E_INVALIDARG;
    }
    std::array<wchar_t, 256> host{};
    if (!Utf8ToWide(domain, host.data(), static_cast<int>(host.size())))
    {
        return E_INVALIDARG;
    }
    const wil::unique_winhttp_hinternet session{WinHttpOpen(L"RedXe/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    (void)WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 15000);
    const wil::unique_winhttp_hinternet connection{
        WinHttpConnect(session.get(), host.data(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    std::array<wchar_t, 64> path{};
    (void)Utf8ToWide(kTokenPath, path.data(), static_cast<int>(path.size()));
    const wil::unique_winhttp_hinternet request{WinHttpOpenRequest(connection.get(), L"POST", path.data(), nullptr,
                                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                   WINHTTP_FLAG_SECURE)};
    if (!request)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    constexpr wchar_t kHeaders[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (!WinHttpSendRequest(request.get(), kHeaders, static_cast<DWORD>(-1), const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    (void)WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    while (received < capacity - 1)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (available == 0)
        {
            break;
        }
        DWORD read = 0;
        const DWORD room = static_cast<DWORD>(capacity - 1 - received);
        if (!WinHttpReadData(request.get(), response + received, available < room ? available : room, &read))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        received += read;
        if (read == 0)
        {
            break;
        }
    }
    response[received] = '\0';
    // A 4xx from the token endpoint still carries a JSON error the parser turns into E_ACCESSDENIED.
    return status == 200 || status == 400 || status == 401 ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT ExchangeCode(ITokenTransport& transport, std::string_view domain, std::string_view clientId,
                     uint32_t redirectPort, std::string_view code, std::string_view verifier, uint64_t nowUnix,
                     TokenSet& tokens) noexcept
{
    if (code.empty() || code.size() > kMaximumCodeBytes || verifier.empty() || clientId.empty())
    {
        return E_INVALIDARG;
    }
    std::array<char, kMaximumUrlBytes + kMaximumCodeBytes> body{};
    std::array<char, 96> redirect{};
    size_t used = 0;
    if (!BuildEncodedRedirectUri(redirectPort, redirect.data(), redirect.size()) ||
        !AppendText(body.data(), body.size(), used, "grant_type=authorization_code&code=") ||
        !AppendUrlEncoded(body.data(), body.size(), used, code) ||
        !AppendText(body.data(), body.size(), used, "&redirect_uri=") ||
        !AppendText(body.data(), body.size(), used, redirect.data()) ||
        !AppendText(body.data(), body.size(), used, "&code_verifier=") ||
        !AppendText(body.data(), body.size(), used, verifier) ||
        !AppendText(body.data(), body.size(), used, "&client_id=") ||
        !AppendUrlEncoded(body.data(), body.size(), used, clientId))
    {
        return E_INVALIDARG;
    }
    return PostForm(transport, domain, body.data(), nowUnix, tokens);
}

HRESULT RefreshTokens(ITokenTransport& transport, std::string_view domain, std::string_view clientId, uint64_t nowUnix,
                      TokenSet& tokens) noexcept
{
    if (tokens.refreshBytes == 0 || clientId.empty())
    {
        return E_NOT_VALID_STATE;
    }
    std::array<char, kMaximumUrlBytes + kMaximumTokenBytes> body{};
    size_t used = 0;
    if (!AppendText(body.data(), body.size(), used, "grant_type=refresh_token&refresh_token=") ||
        !AppendUrlEncoded(body.data(), body.size(), used, tokens.RefreshToken()) ||
        !AppendText(body.data(), body.size(), used, "&client_id=") ||
        !AppendUrlEncoded(body.data(), body.size(), used, clientId))
    {
        return E_INVALIDARG;
    }
    return PostForm(transport, domain, body.data(), nowUnix, tokens);
}

HRESULT CredentialManagerStore::Read(std::string_view clientId, char* refreshToken, size_t capacity,
                                     uint32_t& bytes) noexcept
{
    bytes = 0;
    std::array<wchar_t, 256> target{};
    if (!refreshToken || capacity == 0 ||
        !BuildCredentialTarget(clientId, target.data(), static_cast<int>(target.size())))
    {
        return E_INVALIDARG;
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.data(), CRED_TYPE_GENERIC, 0, &credential))
    {
        const DWORD error = GetLastError();
        return error == ERROR_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);
    }
    const auto freeCredential = wil::scope_exit([&]() noexcept { CredFree(credential); });
    if (credential->CredentialBlobSize == 0 || credential->CredentialBlobSize >= capacity ||
        credential->CredentialBlobSize > kMaximumTokenBytes)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::memcpy(refreshToken, credential->CredentialBlob, credential->CredentialBlobSize);
    refreshToken[credential->CredentialBlobSize] = '\0';
    bytes = credential->CredentialBlobSize;
    return S_OK;
}

HRESULT CredentialManagerStore::Write(std::string_view clientId, std::string_view refreshToken) noexcept
{
    std::array<wchar_t, 256> target{};
    if (refreshToken.empty() || refreshToken.size() > kMaximumTokenBytes ||
        !BuildCredentialTarget(clientId, target.data(), static_cast<int>(target.size())))
    {
        return E_INVALIDARG;
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(refreshToken.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(refreshToken.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    wchar_t userName[] = L"RedXe";
    credential.UserName = userName;
    return CredWriteW(&credential, 0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT CredentialManagerStore::Delete(std::string_view clientId) noexcept
{
    std::array<wchar_t, 256> target{};
    if (!BuildCredentialTarget(clientId, target.data(), static_cast<int>(target.size())))
    {
        return E_INVALIDARG;
    }
    if (!CredDeleteW(target.data(), CRED_TYPE_GENERIC, 0))
    {
        const DWORD error = GetLastError();
        return error == ERROR_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);
    }
    return S_OK;
}

HRESULT MemoryCredentialStore::Read(std::string_view, char* refreshToken, size_t capacity, uint32_t& bytes) noexcept
{
    bytes = 0;
    if (!refreshToken || capacity == 0)
    {
        return E_INVALIDARG;
    }
    if (!_present)
    {
        return S_FALSE;
    }
    if (_bytes >= capacity)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(refreshToken, _token.data(), _bytes);
    refreshToken[_bytes] = '\0';
    bytes = _bytes;
    return S_OK;
}

HRESULT MemoryCredentialStore::Write(std::string_view, std::string_view refreshToken) noexcept
{
    if (refreshToken.empty() || refreshToken.size() > kMaximumTokenBytes)
    {
        return E_INVALIDARG;
    }
    _token.fill('\0');
    std::memcpy(_token.data(), refreshToken.data(), refreshToken.size());
    _bytes = static_cast<uint32_t>(refreshToken.size());
    _present = true;
    return S_OK;
}

HRESULT MemoryCredentialStore::Delete(std::string_view) noexcept
{
    const bool present = _present;
    _present = false;
    _bytes = 0;
    _token.fill('\0');
    return present ? S_OK : S_FALSE;
}

LoopbackListener::~LoopbackListener()
{
    Stop();
}

HRESULT LoopbackListener::Start(uint16_t port) noexcept
{
    if (_listener != ~uintptr_t{0})
    {
        return E_NOT_VALID_STATE;
    }
    if (!_winsock)
    {
        WSADATA data{};
        const int started = WSAStartup(MAKEWORD(2, 2), &data);
        if (started != 0)
        {
            return HRESULT_FROM_WIN32(static_cast<DWORD>(started));
        }
        _winsock = true;
    }
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET)
    {
        return HRESULT_FROM_WIN32(static_cast<DWORD>(WSAGetLastError()));
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int exclusive = 1;
    (void)setsockopt(socketHandle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                     sizeof(exclusive));
    if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(socketHandle, 1) != 0)
    {
        const int error = WSAGetLastError();
        closesocket(socketHandle);
        return HRESULT_FROM_WIN32(static_cast<DWORD>(error));
    }
    sockaddr_in bound{};
    int boundLength = sizeof(bound);
    if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0)
    {
        _port = ntohs(bound.sin_port);
    }
    else
    {
        _port = port;
    }
    const WSAEVENT event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT || WSAEventSelect(socketHandle, event, FD_ACCEPT) != 0)
    {
        const int error = WSAGetLastError();
        if (event != WSA_INVALID_EVENT)
        {
            WSACloseEvent(event);
        }
        closesocket(socketHandle);
        return HRESULT_FROM_WIN32(static_cast<DWORD>(error));
    }
    _listener = static_cast<uintptr_t>(socketHandle);
    _event = event;
    _requestBytes = 0;
    return S_OK;
}

void LoopbackListener::CloseClient() noexcept
{
    if (_client != ~uintptr_t{0})
    {
        shutdown(static_cast<SOCKET>(_client), SD_BOTH);
        closesocket(static_cast<SOCKET>(_client));
        _client = ~uintptr_t{0};
    }
    _requestBytes = 0;
}

void LoopbackListener::Stop() noexcept
{
    CloseClient();
    if (_listener != ~uintptr_t{0})
    {
        closesocket(static_cast<SOCKET>(_listener));
        _listener = ~uintptr_t{0};
    }
    if (_event)
    {
        WSACloseEvent(_event);
        _event = nullptr;
    }
    if (_winsock)
    {
        WSACleanup();
        _winsock = false;
    }
    _port = 0;
}

bool LoopbackListener::Running() const noexcept
{
    return _listener != ~uintptr_t{0};
}

uint16_t LoopbackListener::Port() const noexcept
{
    return _port;
}

HANDLE LoopbackListener::Event() const noexcept
{
    return _event;
}

HRESULT LoopbackListener::Respond(bool ok) noexcept
{
    static constexpr char kOk[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
                                  "Content-Length: 78\r\n\r\n"
                                  "<html><body>RedXe is signed in to Zoom. You can close this window.</body></html>";
    static constexpr char kBad[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n"
                                   "Content-Length: 24\r\n\r\nRedXe: invalid redirect.";
    const char* text = ok ? kOk : kBad;
    const int length = static_cast<int>(ok ? sizeof(kOk) - 1 : sizeof(kBad) - 1);
    // The reply fits one send on a loopback socket; a partial send is not retried.
    const int sent = send(static_cast<SOCKET>(_client), text, length, 0);
    return sent == length ? S_OK : HRESULT_FROM_WIN32(static_cast<DWORD>(WSAGetLastError()));
}

HRESULT LoopbackListener::Pump(std::string_view expectedState, char* code, size_t capacity) noexcept
{
    if (!code || capacity == 0)
    {
        return E_INVALIDARG;
    }
    code[0] = '\0';
    if (!Running())
    {
        return E_NOT_VALID_STATE;
    }
    // Listening socket: accept at most one client at a time.
    WSANETWORKEVENTS events{};
    if (WSAEnumNetworkEvents(static_cast<SOCKET>(_listener), _event, &events) == 0 &&
        (events.lNetworkEvents & FD_ACCEPT) != 0 && _client == ~uintptr_t{0})
    {
        const SOCKET client = accept(static_cast<SOCKET>(_listener), nullptr, nullptr);
        if (client != INVALID_SOCKET)
        {
            if (WSAEventSelect(client, _event, FD_READ | FD_CLOSE) != 0)
            {
                closesocket(client);
            }
            else
            {
                _client = static_cast<uintptr_t>(client);
                _requestBytes = 0;
            }
        }
    }
    if (_client == ~uintptr_t{0})
    {
        return S_FALSE;
    }
    WSANETWORKEVENTS clientEvents{};
    if (WSAEnumNetworkEvents(static_cast<SOCKET>(_client), _event, &clientEvents) != 0)
    {
        CloseClient();
        return S_FALSE;
    }
    if ((clientEvents.lNetworkEvents & (FD_READ | FD_CLOSE)) == 0)
    {
        return S_FALSE;
    }
    const int room = static_cast<int>(_request.size() - 1 - _requestBytes);
    if (room > 0)
    {
        const int read = recv(static_cast<SOCKET>(_client), _request.data() + _requestBytes, room, 0);
        if (read > 0)
        {
            _requestBytes += static_cast<uint32_t>(read);
            _request[_requestBytes] = '\0';
        }
    }
    // The request line is complete once its CRLF arrived (or the peer closed).
    const std::string_view request{_request.data(), _requestBytes};
    const size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string_view::npos && (clientEvents.lNetworkEvents & FD_CLOSE) == 0 && room > 0)
    {
        return S_FALSE;
    }
    const std::string_view line = request.substr(0, lineEnd);
    bool ok = false;
    const size_t query = line.find("GET /redirect?");
    if (query != std::string_view::npos)
    {
        std::string_view parameters = line.substr(query + 14);
        const size_t space = parameters.find(' ');
        parameters = parameters.substr(0, space);
        std::string_view foundCode;
        std::string_view foundState;
        while (!parameters.empty())
        {
            const size_t amp = parameters.find('&');
            const std::string_view pair = parameters.substr(0, amp);
            if (pair.starts_with("code="))
            {
                foundCode = pair.substr(5);
            }
            else if (pair.starts_with("state="))
            {
                foundState = pair.substr(6);
            }
            if (amp == std::string_view::npos)
            {
                break;
            }
            parameters.remove_prefix(amp + 1);
        }
        ok = !foundCode.empty() && foundCode.size() < capacity && foundCode.size() <= kMaximumCodeBytes &&
             foundState == expectedState;
        if (ok)
        {
            std::memcpy(code, foundCode.data(), foundCode.size());
            code[foundCode.size()] = '\0';
        }
    }
    (void)Respond(ok);
    CloseClient();
    if (!ok)
    {
        return S_FALSE;
    }
    Stop();
    return S_OK;
}
} // namespace Zoom
