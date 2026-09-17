#pragma once

// The user OAuth sign-in the Zoom Plugin SDK needs (scope plugin_sdk:read:connection_meta), done the way Zoom
// documents for native apps: PKCE, a loopback redirect, and no client secret anywhere. The refresh token is the only
// durable credential and lives in Windows Credential Manager; the access token stays in the service's memory and is
// refreshed over WinHTTP when it expires. Every piece is bounded and injectable so ZoomTests cover it without a
// network: the transport, the credential store, and the listener (which tests drive with a local client socket).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <windows.h>

namespace Zoom
{
inline constexpr uint32_t kVerifierCharacters = 64;
inline constexpr uint32_t kChallengeCharacters = 43;
inline constexpr uint32_t kStateCharacters = 22;
inline constexpr uint32_t kMaximumTokenBytes = 2048;
inline constexpr uint32_t kMaximumCodeBytes = 512;
inline constexpr uint32_t kMaximumUrlBytes = 1024;
inline constexpr uint32_t kMaximumTokenResponseBytes = 16384;
inline constexpr char kTokenHost[] = "zoom.us";
inline constexpr char kTokenPath[] = "/oauth/token";
inline constexpr char kAuthorizePath[] = "/oauth/authorize";
inline constexpr char kRedirectPath[] = "/redirect";
inline constexpr char kCredentialTargetPrefix[] = "RedXe/Zoom/";

// PKCE and random material (BCrypt). Outputs are NUL-terminated base64url text.
[[nodiscard]] bool Base64UrlEncode(const uint8_t* bytes, size_t count, char* text, size_t capacity) noexcept;
[[nodiscard]] bool GenerateVerifier(char* text, size_t capacity) noexcept;
[[nodiscard]] bool GenerateState(char* text, size_t capacity) noexcept;
// BASE64URL(SHA256(verifier)), the S256 challenge.
[[nodiscard]] bool ComputeChallenge(std::string_view verifier, char* text, size_t capacity) noexcept;
// https://<domain>/oauth/authorize?response_type=code&client_id=...&redirect_uri=...&code_challenge=...&
// code_challenge_method=S256&state=...
[[nodiscard]] bool BuildAuthorizeUrl(std::string_view domain, std::string_view clientId, uint32_t redirectPort,
                                     std::string_view challenge, std::string_view state, char* url,
                                     size_t capacity) noexcept;
// http://127.0.0.1:<port>/redirect, URL-encoded for a form body.
[[nodiscard]] bool BuildEncodedRedirectUri(uint32_t redirectPort, char* text, size_t capacity) noexcept;

struct TokenSet final
{
    std::array<char, kMaximumTokenBytes + 1> accessToken{};
    std::array<char, kMaximumTokenBytes + 1> refreshToken{};
    uint32_t accessBytes = 0;
    uint32_t refreshBytes = 0;
    // Unix seconds at which accessToken expires.
    uint64_t expiresAt = 0;

    [[nodiscard]] std::string_view AccessToken() const noexcept
    {
        return std::string_view(accessToken.data(), accessBytes);
    }
    [[nodiscard]] std::string_view RefreshToken() const noexcept
    {
        return std::string_view(refreshToken.data(), refreshBytes);
    }
};

// Parses a token endpoint response ({"access_token","refresh_token","expires_in",...}). E_INVALIDARG for a
// response without an access token or with overlong tokens; a missing refresh token keeps the previous one.
[[nodiscard]] HRESULT ParseTokenResponse(std::string_view json, uint64_t nowUnix, TokenSet& tokens) noexcept;

// POSTs an application/x-www-form-urlencoded body to https://<domain>/oauth/token and returns the response body.
class ITokenTransport
{
  public:
    virtual ~ITokenTransport() = default;
    [[nodiscard]] virtual HRESULT Post(std::string_view domain, std::string_view body, char* response, size_t capacity,
                                       size_t& received) noexcept = 0;
};

// WinHTTP implementation; one request per call, no session kept between calls.
class WinHttpTokenTransport final : public ITokenTransport
{
  public:
    [[nodiscard]] HRESULT Post(std::string_view domain, std::string_view body, char* response, size_t capacity,
                               size_t& received) noexcept override;
};

// grant_type=authorization_code with the PKCE verifier, then grant_type=refresh_token.
[[nodiscard]] HRESULT ExchangeCode(ITokenTransport& transport, std::string_view domain, std::string_view clientId,
                                   uint32_t redirectPort, std::string_view code, std::string_view verifier,
                                   uint64_t nowUnix, TokenSet& tokens) noexcept;
[[nodiscard]] HRESULT RefreshTokens(ITokenTransport& transport, std::string_view domain, std::string_view clientId,
                                    uint64_t nowUnix, TokenSet& tokens) noexcept;

// Durable storage of the refresh token, keyed by client id.
class ICredentialStore
{
  public:
    virtual ~ICredentialStore() = default;
    // S_OK with the token, S_FALSE when none is stored, or a failure.
    [[nodiscard]] virtual HRESULT Read(std::string_view clientId, char* refreshToken, size_t capacity,
                                       uint32_t& bytes) noexcept = 0;
    [[nodiscard]] virtual HRESULT Write(std::string_view clientId, std::string_view refreshToken) noexcept = 0;
    [[nodiscard]] virtual HRESULT Delete(std::string_view clientId) noexcept = 0;
};

// Windows Credential Manager, generic credential "RedXe/Zoom/<clientId>", per user.
class CredentialManagerStore final : public ICredentialStore
{
  public:
    [[nodiscard]] HRESULT Read(std::string_view clientId, char* refreshToken, size_t capacity,
                               uint32_t& bytes) noexcept override;
    [[nodiscard]] HRESULT Write(std::string_view clientId, std::string_view refreshToken) noexcept override;
    [[nodiscard]] HRESULT Delete(std::string_view clientId) noexcept override;
};

// In-memory store for tests and automated hosts.
class MemoryCredentialStore final : public ICredentialStore
{
  public:
    [[nodiscard]] HRESULT Read(std::string_view clientId, char* refreshToken, size_t capacity,
                               uint32_t& bytes) noexcept override;
    [[nodiscard]] HRESULT Write(std::string_view clientId, std::string_view refreshToken) noexcept override;
    [[nodiscard]] HRESULT Delete(std::string_view clientId) noexcept override;

  private:
    std::array<char, kMaximumTokenBytes + 1> _token{};
    uint32_t _bytes = 0;
    bool _present = false;
};

// The loopback redirect listener: one socket on 127.0.0.1:<port>, one client at a time, event-driven so the
// service lane waits on Event() beside its own handles and never blocks in recv. The lane calls Pump when Event()
// signals; the code arrives through Pump's S_OK.
class LoopbackListener final
{
  public:
    LoopbackListener() = default;
    ~LoopbackListener();
    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;

    [[nodiscard]] HRESULT Start(uint16_t port) noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] uint16_t Port() const noexcept;
    // Signaled on accept, read, and close events of the listening and client sockets.
    [[nodiscard]] HANDLE Event() const noexcept;
    // Handles pending socket events. S_OK: a redirect with a matching state arrived and `code` holds its code
    // (the listener stops). S_FALSE: nothing complete yet, keep waiting. A failure closes the listener.
    [[nodiscard]] HRESULT Pump(std::string_view expectedState, char* code, size_t capacity) noexcept;

  private:
    void CloseClient() noexcept;
    [[nodiscard]] HRESULT Respond(bool ok) noexcept;

    uintptr_t _listener = ~uintptr_t{0};
    uintptr_t _client = ~uintptr_t{0};
    HANDLE _event = nullptr;
    uint16_t _port = 0;
    bool _winsock = false;
    std::array<char, 4096> _request{};
    uint32_t _requestBytes = 0;
};

// Unix seconds now.
[[nodiscard]] uint64_t UnixNow() noexcept;
} // namespace Zoom
