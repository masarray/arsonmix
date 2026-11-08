"""Host-independent Pages metadata and strict public URL normalization.

The published URL comes from actions/configure-pages, not hard-coded routing or
Cloudflare-specific credentials. A custom domain uses / instead of /arsonmix/.
"""
import re
from urllib.parse import urlsplit
from xml.sax.saxutils import escape

DEFAULT_PUBLIC_URL = "https://masarray.github.io/arsonmix/"
OG_IMAGE = '<meta property="og:image" content="' + DEFAULT_PUBLIC_URL + 'brand-512.png">'
OG_URL = '<meta property="og:url" content="' + DEFAULT_PUBLIC_URL + '">'


def normalize_public_url(value):
    raw = (value or DEFAULT_PUBLIC_URL).strip()
    parsed = urlsplit(raw)
    if (parsed.scheme != "https" or not parsed.hostname or
            parsed.username is not None or parsed.password is not None or
            parsed.query or parsed.fragment):
        raise ValueError("Pages public URL must be an HTTPS origin or project base URL")
    try:
        if parsed.port is not None:
            raise ValueError("Pages public URL must not specify a port")
    except ValueError as exc:
        raise ValueError("Pages public URL has an invalid port") from exc
    path = parsed.path.rstrip("/")
    if path and (not re.fullmatch(r"/[A-Za-z0-9_~-]+(?:/[A-Za-z0-9_~-]+)*", path)
                 or any(segment in (".", "..") for segment in path.split("/"))):
        raise ValueError("Pages public URL contains an invalid base path")
    return "https://" + parsed.hostname.lower() + path + "/"


def render_landing_metadata(html, public_url):
    replacements = (
        (OG_IMAGE, '<meta property="og:image" content="' + public_url + 'brand-512.png">'),
        (OG_URL, '<meta property="og:url" content="' + public_url + '">'),
        ('  <title>ArSonMix — Professional Live Mixing Studio</title>',
         '  <link rel="canonical" href="' + public_url + '">\n'
         '  <title>ArSonMix — Professional Live Mixing Studio</title>'),
    )
    for old, new in replacements:
        if html.count(old) != 1:
            raise ValueError("Missing/duplicate landing metadata source: " + old)
        html = html.replace(old, new, 1)
    return html


def sitemap_xml(public_url):
    urls = (public_url, public_url + "preview/")
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
            + "".join("  <url><loc>" + escape(url) + "</loc></url>\n" for url in urls)
            + "</urlset>\n")


def robots_txt(public_url):
    return "User-agent: *\nAllow: /\nSitemap: " + public_url + "sitemap.xml\n"
