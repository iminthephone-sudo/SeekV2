"""Multiple profiles.

A *profile* is one resume persona. Outreach staff usually work with many
participants, and each participant may keep several profiles aimed at
different kinds of work (e.g. "Warehouse" and "Kitchen"). Profiles carry a
``participant`` label so the shell can group them.
"""

from __future__ import annotations

import copy
from typing import Any

from ..storage import NotFound, Store, as_text, new_id, utc_now

COLLECTION = "profiles"

# Section name -> fields for each entry. Also consumed by the shell to build forms.
SECTION_FIELDS: dict[str, list[str]] = {
    "experience": ["title", "employer", "location", "start", "end", "bullets"],
    "education": ["credential", "school", "field", "location", "start", "end", "details"],
    "certifications": ["name", "issuer", "date", "expires"],
    "training": ["name", "provider", "hours", "date", "details"],
    "volunteer": ["role", "organization", "start", "end", "bullets"],
    "references": ["name", "relationship", "phone", "email"],
}
CONTACT_FIELDS = ["full_name", "email", "phone", "city", "state", "linkedin", "website"]
# Job sites a profile can be signed in to (through the shell's built-in browser). SEEK stores only the
# status; the site's own sign-in cookie stays in that browser's storage for this profile.
ACCOUNT_SITES = {"linkedin": "LinkedIn", "indeed": "Indeed"}
ACCOUNT_STATUSES = ("signed_in", "signed_out", "unknown")
TEMPLATES = ["classic", "modern", "compact"]


def blank_profile(name: str = "New profile", participant: str = "") -> dict[str, Any]:
    now = utc_now()
    return {
        "id": new_id("prof"),
        "name": name,
        "participant": participant,
        "created_at": now,
        "updated_at": now,
        "contact": {f: "" for f in CONTACT_FIELDS},
        "headline": "",
        "summary": "",
        "target_roles": [],
        "skills": [],
        **{section: [] for section in SECTION_FIELDS},
        "notes": "",
        "options": {"template": "classic", "include_references": False, "references_on_request": True},
        "accounts": {},
    }


def _clean_text(value: Any) -> str:
    return as_text(value).strip()


def _clean_list(value: Any) -> list[str]:
    if isinstance(value, str):
        value = value.splitlines()
    if not isinstance(value, list):
        return []
    seen, out = set(), []
    for item in value:
        text = _clean_text(item).lstrip("-•*· ").strip()
        if text and text.lower() not in seen:
            seen.add(text.lower())
            out.append(text)
    return out


def normalize(profile: dict[str, Any]) -> dict[str, Any]:
    """Coerce shell-provided data into the canonical profile shape."""
    base = blank_profile()
    out: dict[str, Any] = {}
    for key in ("id", "name", "participant", "created_at", "updated_at", "headline", "summary", "notes"):
        out[key] = _clean_text(profile.get(key, base[key])) or base[key]
    out["name"] = out["name"] or "Untitled profile"
    contact = profile.get("contact") or {}
    out["contact"] = {f: _clean_text(contact.get(f)) for f in CONTACT_FIELDS}
    out["target_roles"] = _clean_list(profile.get("target_roles"))
    out["skills"] = _clean_list(profile.get("skills"))
    for section, fields in SECTION_FIELDS.items():
        entries = []
        for entry in profile.get(section) or []:
            if not isinstance(entry, dict):
                continue
            clean = {"id": _clean_text(entry.get("id")) or new_id(section[:3])}
            for f in fields:
                clean[f] = _clean_list(entry.get(f)) if f == "bullets" else _clean_text(entry.get(f))
            if any(v for k, v in clean.items() if k != "id"):
                entries.append(clean)
        out[section] = entries
    options = {**base["options"], **(profile.get("options") or {})}
    if options.get("template") not in TEMPLATES:
        options["template"] = "classic"
    out["options"] = options
    accounts = {}
    for site, info in (profile.get("accounts") or {}).items():
        if site in ACCOUNT_SITES and isinstance(info, dict) and info.get("status") in ACCOUNT_STATUSES:
            accounts[site] = {"status": info["status"], "checked_at": _clean_text(info.get("checked_at"))}
    out["accounts"] = accounts
    return out


class ProfileEngine:
    def __init__(self, store: Store) -> None:
        self.store = store

    def _require(self, profile_id: str) -> dict[str, Any]:
        profile = self.store.get(COLLECTION, profile_id)
        if profile is None:
            raise NotFound(f"profile not found: {profile_id}")
        return profile

    def list(self) -> list[dict[str, Any]]:
        active = self.store.settings().get("active_profile")
        rows = []
        for p in self.store.all(COLLECTION):
            rows.append({
                "id": p["id"], "name": p.get("name", ""), "participant": p.get("participant", ""),
                "full_name": p.get("contact", {}).get("full_name", ""), "headline": p.get("headline", ""),
                "updated_at": p.get("updated_at", ""), "active": p["id"] == active,
                "experience_count": len(p.get("experience", [])), "skills_count": len(p.get("skills", [])),
            })
        rows.sort(key=lambda r: (r["participant"].lower(), r["name"].lower()))
        return rows

    def get(self, profile_id: str) -> dict[str, Any]:
        return self._require(profile_id)

    def create(self, name: str = "", participant: str = "", data: dict | None = None) -> dict[str, Any]:
        profile = blank_profile(name or (data or {}).get("name") or "New profile", participant)
        if data:
            merged = {**profile, **data, "id": profile["id"], "created_at": profile["created_at"],
                      # Explicit arguments win over whatever the data payload carries.
                      "name": name or data.get("name") or profile["name"],
                      "participant": participant or data.get("participant", "")}
            profile = normalize(merged)
        self.store.put(COLLECTION, profile["id"], profile)
        if not self.store.settings().get("active_profile"):
            self.store.update_settings(active_profile=profile["id"])
        self.store.append_history("profile", f"Created profile '{profile['name']}'", profile_id=profile["id"])
        return profile

    def update(self, profile_id: str, changes: dict[str, Any]) -> dict[str, Any]:
        current = self._require(profile_id)
        merged = {**current, **changes, "id": profile_id, "created_at": current.get("created_at")}
        if "contact" in changes:
            merged["contact"] = {**current.get("contact", {}), **(changes.get("contact") or {})}
        if "options" in changes:
            merged["options"] = {**current.get("options", {}), **(changes.get("options") or {})}
        profile = normalize(merged)
        profile["updated_at"] = utc_now()
        self.store.put(COLLECTION, profile_id, profile)
        return profile

    def delete(self, profile_id: str) -> dict[str, Any]:
        profile = self._require(profile_id)
        self.store.delete(COLLECTION, profile_id)
        if self.store.settings().get("active_profile") == profile_id:
            remaining = self.list()
            self.store.update_settings(active_profile=remaining[0]["id"] if remaining else "")
        self.store.append_history("profile", f"Deleted profile '{profile.get('name')}'", profile_id=profile_id)
        return {"deleted": profile_id}

    def duplicate(self, profile_id: str, name: str = "") -> dict[str, Any]:
        source = copy.deepcopy(self._require(profile_id))
        source["name"] = name or f"{source.get('name', 'Profile')} (copy)"
        source.pop("id", None)
        source.pop("accounts", None)  # the copy has its own (empty) browser storage, so it starts signed out
        clone = self.create(source["name"], source.get("participant", ""), source)
        self.store.append_history("profile", f"Duplicated '{self._require(profile_id)['name']}' as '{clone['name']}'",
                                  profile_id=clone["id"], source_id=profile_id)
        return clone

    def set_active(self, profile_id: str) -> dict[str, Any]:
        self._require(profile_id)
        self.store.update_settings(active_profile=profile_id)
        return {"active_profile": profile_id}

    def set_account(self, profile_id: str, site: str, status: str) -> dict[str, Any]:
        """Record whether this profile is signed in to a job site (the shell checks; the engine remembers)."""
        if site not in ACCOUNT_SITES:
            raise ValueError(f"site must be one of {sorted(ACCOUNT_SITES)}")
        if status not in ACCOUNT_STATUSES:
            raise ValueError(f"status must be one of {list(ACCOUNT_STATUSES)}")
        profile = self._require(profile_id)
        before = (profile.get("accounts") or {}).get(site, {}).get("status")
        profile.setdefault("accounts", {})[site] = {"status": status, "checked_at": utc_now()}
        self.store.put(COLLECTION, profile_id, profile)
        if before != status and status != "unknown":
            verb = "Signed in to" if status == "signed_in" else "Signed out of"
            self.store.append_history("profile", f"{verb} {ACCOUNT_SITES[site]} for '{profile.get('name')}'",
                                      profile_id=profile_id, site=site)
        return profile["accounts"]

    def active(self) -> dict[str, Any] | None:
        pid = self.store.settings().get("active_profile")
        return self.store.get(COLLECTION, pid) if pid else None


def profile_text(profile: dict[str, Any]) -> str:
    """Flatten a profile into prose for NLP matching."""
    parts: list[str] = [profile.get("headline", ""), profile.get("summary", "")]
    parts += profile.get("target_roles", [])
    parts.append(". ".join(profile.get("skills", [])))
    for exp in profile.get("experience", []):
        parts.append(f"{exp.get('title', '')} at {exp.get('employer', '')}.")
        parts += exp.get("bullets", [])
    for vol in profile.get("volunteer", []):
        parts.append(f"{vol.get('role', '')} at {vol.get('organization', '')}.")
        parts += vol.get("bullets", [])
    for edu in profile.get("education", []):
        parts.append(" ".join(filter(None, [edu.get("credential"), edu.get("field"), edu.get("school"), edu.get("details")])))
    for cert in profile.get("certifications", []):
        parts.append(" ".join(filter(None, [cert.get("name"), cert.get("issuer")])))
    for tr in profile.get("training", []):
        parts.append(" ".join(filter(None, [tr.get("name"), tr.get("provider"), tr.get("details")])))
    return "\n".join(p.strip().rstrip(".") + "." for p in parts if p and p.strip())
