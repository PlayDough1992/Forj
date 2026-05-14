# Forj — Compliance Checklist

Last updated: 2026-05-14

---

## US Federal / Congressional Requirements

| # | Requirement | Status | Notes |
|---|-------------|--------|-------|
| 1 | **Age verification (KYC)** — government ID + face match | ✅ Done | Didit session flow, 500 free/month |
| 2 | **No anonymous accounts** — real identity tied to every account | ✅ Done | KYC required before any API access |
| 3 | **KYC middleware** — entire API blocked until verified | ✅ Done | `kyc_required_middleware` in `server/main.py` |
| 4 | **Token invalidation on status change** | ✅ Done | JWT `ver` claim; password/KYC change invalidates all sessions |
| 5 | **Content safety filtering** | ✅ Done | Regex pattern matching + freeze threshold in `server/safety.py` |
| 6 | **HMAC-signed webhook verification** | ✅ Done | Didit webhook HMAC-SHA256 in `server/main.py` |
| 7 | **CSAM hash-scanning (PhotoDNA / Azure Content Safety)** | ❌ Pending | Required before enabling file/image uploads in chat |
| 8 | **NCMEC CyberTipline reporting pipeline** | ❌ Pending | Required when CSAM is detected (18 U.S.C. § 2258A) |
| 9 | **User reporting system** — flag illegal/abusive content | ❌ Pending | Must exist before public launch |
| 10 | **COPPA data handling policy** — formal policy for under-13 data | ❌ Pending | KYC blocks under-13 in practice; formal policy still needed |

---

## Authentication & Access

| # | Requirement | Status | Notes |
|---|-------------|--------|-------|
| 11 | **Bcrypt password hashing** | ✅ Done | |
| 12 | **JWT authentication** | ✅ Done | `python-jose`, HS256 |
| 13 | **Two-factor authentication (TOTP/SMS)** | ❌ Pending | |
| 14 | **Email verification on registration** | ❌ Pending | |
| 15 | **Rate limiting on API endpoints** | ❌ Pending | Highest-priority security gap |
| 16 | **Suspicious login detection** (new IP/device alerts) | ❌ Pending | |

---

## Privacy & Data

| # | Requirement | Status | Notes |
|---|-------------|--------|-------|
| 17 | **Privacy Policy** | ❌ Pending | Required before public launch |
| 18 | **Terms of Service** | ❌ Pending | Required before public launch |
| 19 | **Data retention / deletion policy** | ❌ Pending | Required under CCPA and similar state laws |
| 20 | **Right to erasure / account deletion** | ❌ Pending | CCPA/GDPR requirement |

---

## Planned Features (security-relevant)

| # | Feature | Notes |
|---|---------|-------|
| A | **File/image uploads in chat** | Must integrate CSAM scanning (item 7) before enabling |
| B | **End-to-end encryption for DMs** | Would limit server-side moderation ability — legal tradeoffs to consider |
| C | **Phone verification** | Strengthens identity beyond KYC alone |

---

## Priority Order for Next Steps

1. **Rate limiting** — No other security feature matters if endpoints can be brute-forced
2. **Email verification** — Basic trust signal, low effort
3. **CSAM scanning** — Required before any file sharing feature ships (Azure Content Safety API)
4. **User reporting system** — Needed before public launch
5. **Privacy Policy + ToS** — Legal prerequisites for any public deployment
6. **2FA** — Standard expectation for any modern platform
