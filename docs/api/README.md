# API Documentation

**🌐 Developer Reference**

---

## Purpose

This folder contains **API documentation** for CoinTrace software interfaces. All content is open-source under **CC BY-SA 4.0** license.

---

## 📂 Planned Content

### Firmware API:
- `firmware-api.md` - ESP32 firmware functions reference
- `serial-protocol.md` - USB serial communication protocol
- `data-structures.md` - Fingerprint vector format, JSON schemas

### Cloud API (Premium):
- `cloud-api-reference.md` - REST API endpoints (premium database)
- `authentication.md` - API key management, OAuth
- `rate-limits.md` - Request limits, quotas
- `webhook-events.md` - Real-time notifications

### Database Schema:
- `database-schema.md` - Coin fingerprint database structure (CC BY-SA)
- `community-database-api.md` - Free tier API (open)
- `contributing-fingerprints.md` - How to submit measurements

### Integration Examples:
- `examples/python/` - Python API client examples
- `examples/javascript/` - Node.js / web examples
- `examples/desktop-app/` - C++/Qt integration
- `examples/mobile/` - Android/iOS integration (future)

### Protocol Specifications:
- `measurement-protocol.md` - 4-point measurement sequence
- `calibration-protocol.md` - Device calibration procedures
- `error-codes.md` - Firmware error codes reference

---

## 🔓 Access Levels

### Open (Free) API:
- Firmware serial protocol ✅
- Community database schema ✅
- Basic coin identification ✅
- Limited to 100 requests/day (no auth required)

### Premium API:
- Full coin database (5000+ coins) 🔒
- Unlimited requests 🔒
- Real-time sync 🔒
- Authentication required 🔒
- Subscription: $5-10/month

---

## 📝 Contributing

API improvements welcome! Guidelines:
- OpenAPI 3.0 specification format preferred
- Provide code examples in multiple languages
- Test on actual hardware/cloud endpoints
- Document authentication clearly

---

## ⚖️ License

**API Documentation:** CC BY-SA 4.0  
**API Client Code:** GPL v3 (open-source)  
**API Server Code:** Proprietary (not published)

**Note:** The API *specification* is open (CC BY-SA), but the server implementation is proprietary. You can create your own compatible server if you want!

---

**Created:** 2026-03-09
