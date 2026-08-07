"""Shared category-only privacy classification for public release text."""

import re
from urllib.parse import unquote, urlsplit


_MAX_URL_DECODE_PASSES = 3
_MAX_URL_COMPONENT_BYTES = 4_096
_ESCAPED_BYTE_RE = re.compile(r"%[0-9A-Fa-f]{2}")
_PUBLIC_URL_RE = re.compile(r"https?://[^\s`]+")
_PRIVATE_PATH_RE = re.compile(r"(?<![A-Za-z0-9])(?:~[\\/]|/(?:[^\s`]+)|[A-Za-z]:[\\/][^\s`]*)")
_TRAVERSAL_PATH_RE = re.compile(r"(?<![A-Za-z0-9])\.\.[\\/]")
_PRIVATE_URL_PATH_RE = re.compile(
    r"(?i)(?:^|/)(?:etc|home|private|tmp|users|var)(?:/|$)|(?:^|/)[A-Za-z]:[\\/]|(?:^|/)~(?:/|$)"
)
_IPV4_RE = re.compile(r"(?<![0-9])(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})(?:\.(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})){3}(?![0-9])")
_IPV6_RE = re.compile(r"(?<![A-Za-z0-9])(?:[0-9A-Fa-f]{1,4}:){2,}[0-9A-Fa-f:]*")
_MAC_RE = re.compile(r"(?<![0-9A-Fa-f])(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f])")
_ROM_BIOS_RE = re.compile(r"(?i)\b(?:rom|bios)(?:\s+(?:identity|hash|sha(?:-?256)?|crc(?:32)?|dump)|\s*:)")
_SAVE_RE = re.compile(r"(?i)\b(?:save[ -]?(?:file|state)|\.sav)\b|\bsave\s+(?:identity|hash|sha(?:-?256)?|crc(?:32)?|dump|data)\b")
_INPUT_RE = re.compile(r"(?i)\b(?:raw input|input recording|input history)\b")
_LOG_RE = re.compile(r"(?i)\b(?:endpoint|frontend|retroarch) log\b")
_DEVICE_RE = re.compile(r"(?i)\b(?:device|phone) (?:serial|nickname|id|name)\b")
_COMMERCIAL_RE = re.compile(r"(?i)\bcommercial (?:game|title|evidence)\b")
_SECRET_RE = re.compile(r"(?i)\b(?:access )?(?:api[_ -]?key|token|secret|password)(?:\s*[:=]|\s+\S+)")


def _decoded_url_component(value: str) -> str | None:
    """Decode a bounded URL component, rejecting unresolved escape nesting."""
    if len(value.encode("utf-8")) > _MAX_URL_COMPONENT_BYTES:
        return None
    for _ in range(_MAX_URL_DECODE_PASSES):
        decoded = unquote(value)
        if len(decoded.encode("utf-8")) > _MAX_URL_COMPONENT_BYTES:
            return None
        if decoded == value:
            return None if _ESCAPED_BYTE_RE.search(value) else value
        value = decoded
    return None if _ESCAPED_BYTE_RE.search(value) else value


def classify_private_text(text: str) -> str | None:
    """Return a category without returning any source text or sensitive value."""
    for url in _PUBLIC_URL_RE.findall(text):
        try:
            parts = urlsplit(url)
        except ValueError:
            return "PATH"
        if parts.username is not None or parts.password is not None:
            return "PATH"
        path = _decoded_url_component(parts.path)
        query = _decoded_url_component(parts.query)
        fragment = _decoded_url_component(parts.fragment)
        if path is None or query is None or fragment is None:
            return "PATH"
        if _PRIVATE_URL_PATH_RE.search(path) or _TRAVERSAL_PATH_RE.search(path):
            return "PATH"
        url_material = query + "\n" + fragment
        if _PRIVATE_PATH_RE.search(url_material) or _TRAVERSAL_PATH_RE.search(url_material):
            return "PATH"
    public_url_free = _PUBLIC_URL_RE.sub("", text)
    if _PRIVATE_PATH_RE.search(public_url_free) or _TRAVERSAL_PATH_RE.search(public_url_free):
        return "PATH"
    if _IPV4_RE.search(text) or _IPV6_RE.search(text) or _MAC_RE.search(text):
        return "ADDRESS"
    for expression, category in (
        (_ROM_BIOS_RE, "ROM_BIOS"),
        (_SAVE_RE, "SAVE"),
        (_INPUT_RE, "INPUT"),
        (_LOG_RE, "LOG"),
        (_DEVICE_RE, "DEVICE"),
        (_COMMERCIAL_RE, "COMMERCIAL"),
        (_SECRET_RE, "SECRET"),
    ):
        if expression.search(text):
            return category
    return None
