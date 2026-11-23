"""Host-independent Pages metadata and strict public URL normalization.

The published URL comes from actions/configure-pages, not hard-coded routing or
Cloudflare-specific credentials. A custom domain uses / instead of /arsonmix/.
"""
import json
import re
from public_fx_pages import FX_SLUGS, FX_PAGES, SCENES
from urllib.parse import urlsplit
from xml.sax.saxutils import escape

DEFAULT_PUBLIC_URL = "https://masarray.github.io/arsonmix/"
OG_IMAGE = '<meta property="og:image" content="' + DEFAULT_PUBLIC_URL + 'media/console-overview.jpg">'
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
        (OG_IMAGE, '<meta property="og:image" content="' + public_url + 'media/console-overview.jpg">'),
        (OG_URL, '<meta property="og:url" content="' + public_url + '">'),
        ('  <title>ArSonMix — Free Open Source Digital Mixer for Windows</title>',
         '  <link rel="canonical" href="' + public_url + '">\n'
         '  <title>ArSonMix — Free Open Source Digital Mixer for Windows</title>'),
    )
    for old, new in replacements:
        if html.count(old) != 1:
            raise ValueError("Missing/duplicate landing metadata source: " + old)
        html = html.replace(old, new, 1)
    # Public, schema.org-aligned product identity: not star ratings, fake
    # downloads, installed-build parity or unsupported feature claims.
    # URLs stay on the configured approved Pages host.
    software = {
        "@context": "https://schema.org",
        "@type": "SoftwareApplication",
        "@id": public_url + "#software",
        "name": "ArSonMix",
        "url": public_url,
        "image": public_url + "media/console-overview.jpg",
        "description": "Free open-source digital mixer for Windows live vocals, karaoke, streaming and instruments.",
        "applicationCategory": "MultimediaApplication",
        "operatingSystem": "Windows 10, Windows 11",
        "license": public_url + "LICENSE.txt",
        "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
    }
    if html.count("</head>") != 1 or "application/ld+json" in html:
        raise ValueError("Unrecognized/duplicate software metadata source")
    json_ld = json.dumps(software, ensure_ascii=False).replace("<", "\\u003c")
    return html.replace("</head>", '  <script type="application/ld+json">'
                        + json_ld + '</script>\n</head>', 1)


def sitemap_xml(public_url):
    """HTML-first discoverability plus official Google image sitemap extension.

    One URL per page, with its relevant genuine visual assets. The optional
    image elements never replace crawlable HTML <img src> fallbacks.
    """
    details = {page["slug"]: page for page in FX_PAGES}
    resources = [(public_url, ("console-overview.jpg", "console-routing.jpg",
                                "console-faders.jpg")),
                 (public_url + "preview/", ())]
    for slug in FX_SLUGS:
        pics = [details[slug]["image"]]
        if slug == "immersib":
            pics.extend(filename for _, filename in SCENES)
        # The lead concert scene also appears in the 6-scene venue gallery.
        unique_pics = tuple(dict.fromkeys(pics))
        resources.append((public_url + "effects/" + slug + "/", unique_pics))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" '
             'xmlns:image="http://www.google.com/schemas/sitemap-image/1.1">']
    for page_url, images in resources:
        lines.append("  <url><loc>" + escape(page_url) + "</loc>")
        for file in images:
            lines.append("    <image:image><image:loc>" +
                         escape(public_url + "media/" + file) +
                         "</image:loc></image:image>")
        lines.append("  </url>")
    return "\n".join(lines + ["</urlset>", ""])


def robots_txt(public_url):
    return "User-agent: *\nAllow: /\nSitemap: " + public_url + "sitemap.xml\n"
