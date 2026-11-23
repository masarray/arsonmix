"""Search-indexable, honest ArSonMix built-in FX guides for the public website.

These are *documentation* pages, never alternative mixer renderers. Keep native
screen evidence separate from independently verified DSP/installer claims.
"""
from html import escape
from pathlib import Path
import json

FX_PAGES = (
    dict(slug="8d-sonku", title="8D-SonKu", category="Spatial headphones / 8D audio",
         seo="8D-SonKu 8D Audio Effect | ArSonMix Digital Mixer",
         description="Explore ArSonMix 8D-SonKu, a built-in 8D audio plugin for headphone spatial effects. See original orbit controls and the free Windows digital mixer.",
         headline="Move sound around your headphones.",
         introduction="8D-SonKu is a built-in 8D audio plugin for positioning and moving sound around a headphone listening field. Its editor makes the direction and movement controls visible rather than hiding them inside a preset name.",
         details=("Explore the binaural map and orbit controls in the editor.", "Use headphones when evaluating spatial placement and movement.", "Adjust levels carefully; spatial effects do not replace routing and output setup."),
         image="fx-editor-8d-sonku.jpg", width=790, height=604, origin="Approved native WebView resource rendered in Chromium; not a hardware test",
         related=("vocal-delay", "immersib")),
    dict(slug="vocal-delay", title="Vocal Delay", category="Voice / creative timing",
         seo="Vocal Delay Built-in Audio Effect | ArSonMix Mixer",
         description="Discover ArSonMix Vocal Delay for live voice and expressive delay throws. Explore the original editor and timing controls before downloading the free digital mixer.",
         headline="Make every phrase leave a trace.",
         introduction="Vocal Delay adds echo and delay options for moments when a vocal phrase deserves more room. The built-in editor shows timing controls and a dedicated THROW action that makes the effect easy to identify in the FX workflow.",
         details=("Review the timing and THROW controls in the original editor.", "Use delay sparingly to keep spoken words and vocals intelligible.", "Manage effect power and level through the mixer FX workflow."),
         image="fx-editor-vocal-delay.jpg", width=778, height=546, origin="Approved native WebView resource rendered in Chromium; not a hardware test",
         related=("harmonice", "compressor")),
    dict(slug="harmonice", title="Harmonice", category="Voice / harmony & doubling",
         seo="Harmonice Vocal Harmony Effect | ArSonMix",
         description="Meet Harmonice, ArSonMix's built-in vocal harmony and doubling effect. View its key wheel and original Windows editor resources.",
         headline="Create a fuller vocal texture.",
         introduction="Harmonice is the vocal harmony and doubling effect in ArSonMix. Its editor presents musical key selection and harmony-related controls in one view so you can work on vocal layers without leaving the mixer.",
         details=("Inspect the musical key wheel and harmony controls.", "Start with a simple vocal arrangement before adding more layers.", "Check the effect in context of the main vocal and master output."),
         image="fx-editor-harmonice.jpg", width=800, height=346, origin="Approved WebView editor resource rendered with accepted test transport readback",
         related=("vocal-delay", "channel-eq")),
    dict(slug="channel-eq", title="Parametric EQ", category="Tone / frequency shaping",
         seo="Parametric EQ Built-in Effect | ArSonMix Mixer",
         description="See ArSonMix Parametric EQ: visual frequency response and channel tone controls inside the free open-source digital mixer for Windows.",
         headline="Find the space in every channel.",
         introduction="Parametric EQ provides a visual place to shape a channel's frequency balance. The original editor combines filter controls with a response curve so EQ adjustments are easier to understand.",
         details=("See how the response curve communicates tone shaping.", "Make small changes while comparing the dry and processed channel.", "Use EQ alongside channel gain and the mix balance."),
         image="fx-editor-channel-eq.jpg", width=795, height=424, origin="Approved native WebView editor resource rendered in Chromium",
         related=("graphic-eq31", "compressor")),
    dict(slug="compressor", title="Compressor", category="Dynamics / level control",
         seo="Compressor Built-in Dynamics Effect | ArSonMix",
         description="Explore the Compressor built into ArSonMix, with a visual dynamics curve and familiar level-control parameters for live audio mixing.",
         headline="Keep dynamic sound under control.",
         introduction="The built-in Compressor helps shape signal dynamics as part of the digital mix workflow. Its editor displays a transfer curve and compression parameters so the relationship between input and output levels remains visible.",
         details=("Review the dynamics transfer curve before setting stronger compression.", "Compare processed level with bypassed level at safe monitoring volume.", "Balance the compressor with upstream channel gain and downstream faders."),
         image="fx-editor-compressor.jpg", width=795, height=377, origin="Approved native WebView editor resource rendered in Chromium",
         related=("channel-eq", "vocal-delay")),
    dict(slug="graphic-eq31", title="Graphic EQ 31", category="Tone / 31-band equalizer",
         seo="31-Band Graphic EQ Audio Effect | ArSonMix",
         description="Explore ArSonMix Graphic EQ 31, a built-in 31-band equalizer with hardware-style sliders and clearly labeled frequency bands.",
         headline="Shape the spectrum, band by band.",
         introduction="Graphic EQ 31 displays thirty-one frequency bands in a single hardware-style equalizer panel. It is designed for people who prefer seeing many fixed-band tone adjustments at once.",
         details=("Inspect the original thirty-one labeled frequency faders.", "Make controlled changes to individual bands.", "Use the equalizer in the context of overall output balance."),
         image="fx-editor-graphic-eq31.jpg", width=1430, height=280, origin="Approved native WebView editor resource rendered in Chromium",
         related=("channel-eq", "compressor")),
    dict(slug="immersib", title="ImmerSIB", category="Venue / concert spatial enhancement",
         seo="ImmerSIB Concert & Venue Spatial Audio Effect | ArSonMix",
         description="Explore six actual ImmerSIB native Windows editor screenshots: concert halls, cinema, arena and front-row listening scenes in ArSonMix.",
         headline="Explore the sound of a venue.",
         introduction="ImmerSIB presents concert, theater and arena-inspired listening scenes in a dedicated native Windows editor. Its visible controls include stage width, distance, early room, late room, spatial mix and master gain.",
         details=("Choose a listening scene and compare its visible settings.", "Inspect stage width, distance, room and spatial controls.", "These owner-provided Windows captures display AUDIO / LATEST EDIT QUEUED, not evidence that a physical audio path was verified."),
         image="fx-editor-immersib-concert-front-row-xl.jpg", width=937, height=610, origin="Owner-provided genuine Windows JUCE editor screenshot; release installer identity not independently verified",
         related=("8d-sonku", "guitar-fx")),
    dict(slug="guitar-fx", title="Guitar FX Rig", category="Guitar / amplifier tone",
         seo="Guitar FX Rig and Amplifier Editor | ArSonMix",
         description="See the real Guitar FX Rig native Windows editor in ArSonMix, including gate, overdrive, amplifier, cabinet and Clean Studio tone controls.",
         headline="Your guitar tone, in one rig.",
         introduction="Guitar FX Rig brings a visible signal-chain layout into ArSonMix, with gate, overdrive, amplifier and cabinet sections. The Clean Studio screenshot highlights the black-and-copper amplifier controls and selectable factory tone ideas.",
         details=("Review the gate, overdrive, amplifier and cabinet chain in the editor.", "Explore the Clean Studio, Warm Blues, Vintage Crunch and Modern Lead visual tone choices.", "The supplied screenshot explicitly reads DSP OFFLINE and PCM OFFLINE; it does not prove active guitar audio processing."),
         image="fx-editor-guitar-rig-clean-studio.jpg", width=1062, height=672, origin="Owner-provided genuine Windows JUCE editor screenshot showing DSP OFFLINE",
         related=("channel-eq", "compressor")),
)
FX_SLUGS = tuple(item["slug"] for item in FX_PAGES)
MAP = {item["slug"]: item for item in FX_PAGES}
SCENES = (
    ("Front Row Power", "fx-editor-immersib-front-row-power.jpg"),
    ("Symphonic Palace", "fx-editor-immersib-symphonic-palace.jpg"),
    ("Velvet Cinema", "fx-editor-immersib-velvet-cinema.jpg"),
    ("Arena Thunder", "fx-editor-immersib-arena-thunder.jpg"),
    ("Royal Concert Hall", "fx-editor-immersib-royal-concert-hall.jpg"),
    ("Concert Front Row XL", "fx-editor-immersib-concert-front-row-xl.jpg"),
)


def page_html(item, public_url):
    path = "effects/" + item["slug"] + "/"
    canonical = public_url + path
    main_image = public_url + "media/" + item["image"]
    title = escape(item["seo"])
    description = escape(item["description"], quote=True)
    name = escape(item["title"])
    cat = escape(item["category"])
    intro = escape(item["introduction"])
    details = "".join("<li>" + escape(line) + "</li>" for line in item["details"])
    related = "".join(
        '<a class="detail-related" href="../' + escape(key) + '/">'
        + escape(MAP[key]["title"]) + '<span aria-hidden="true">↗</span></a>'
        for key in item["related"]
    )
    gallery = ""
    if item["slug"] == "immersib":
        gallery = ('<section class="detail-scenes" aria-labelledby="scenes-title">'
                   '<div class="detail-section-title"><span>LISTENING SCENES</span>'
                   '<h2 id="scenes-title">Six ImmerSIB Windows editor views</h2>'
                   '<p>Real original screenshots supplied by the project owner. '
                   'These are UI captures, not verified acoustic measurements.</p></div>'
                   '<div class="detail-scene-grid">'
                   + "".join(
                       '<a href="../../media/' + file + '" target="_blank" rel="noopener">'
                       '<img src="../../media/' + file + '" width="937" height="610" '
                       'loading="lazy" decoding="async" alt="ImmerSIB Windows native editor, '
                       + escape(label) + ' listening scene">'
                       '<span>' + escape(label) + '</span></a>'
                       for label, file in SCENES
                   ) + '</div></section>')
    note = ('Actual native Windows editor screenshot supplied by the owner. '
            'The image does not establish real-time DSP operation or installed release parity.'
            if item["slug"] in ("immersib", "guitar-fx") else
            'Original native WebView editor source rendered under a controlled browser test. '
            'The picture is not evidence of live hardware or DSP validation.')
    if item["slug"] == "guitar-fx":
        note += ' The visible Guitar FX screen reads DSP OFFLINE and PCM OFFLINE.'
    structured = json.dumps({
        "@context": "https://schema.org", "@type": "BreadcrumbList",
        "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "ArSonMix", "item": public_url},
            {"@type": "ListItem", "position": 2, "name": "Built-in FX", "item": public_url + "#effects"},
            {"@type": "ListItem", "position": 3, "name": item["title"], "item": canonical},
        ]
    }, ensure_ascii=False).replace("<", "\\u003c")
    return f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#09131b">
  <meta name="description" content="{description}">
  <link rel="canonical" href="{canonical}">
  <meta property="og:type" content="article">
  <meta property="og:title" content="{title}">
  <meta property="og:description" content="{description}">
  <meta property="og:url" content="{canonical}">
  <meta property="og:image" content="{main_image}">
  <meta name="twitter:card" content="summary_large_image">
  <title>{title}</title>
  <link rel="icon" href="../../favicon.ico" sizes="any">
  <link rel="stylesheet" href="../../landing.css">
  <link rel="stylesheet" href="../../fx-detail.css">
  <script type="application/ld+json">{structured}</script>
</head>
<body class="fx-detail-page">
  <a class="skip-link" href="#detail-content">Skip to content</a>
  <header class="detail-topbar">
    <div class="detail-wrap detail-topbar-content">
      <a class="detail-brand" href="../../" aria-label="ArSonMix home"><img src="../../arsonmix-128.png" width="32" height="32" alt="" aria-hidden="true">Ar<span>Son</span>Mix</a>
      <nav aria-label="Effect navigation"><a href="../../#effects">All built-in FX</a><a href="../../preview/">Mixer preview</a><a class="detail-cta" href="../../#download">Free download</a></nav>
    </div>
  </header>
  <main class="detail-wrap" id="detail-content">
    <nav class="detail-crumbs" aria-label="Breadcrumb"><a href="../../">Home</a><span>/</span><a href="../../#effects">Built-in FX</a><span>/</span><span aria-current="page">{name}</span></nav>
    <section class="detail-hero">
      <div class="detail-intro">
        <span class="detail-kicker">{cat}</span>
        <h1>{name}</h1>
        <p class="detail-headline">{escape(item["headline"])}</p>
        <p class="detail-description">{intro}</p>
        <div class="detail-actions"><a class="btn btn-primary" href="../../#download">Get ArSonMix</a><a class="btn btn-outline" href="../../preview/">Explore mixer</a></div>
      </div>
      <figure class="detail-preview">
        <a href="../../media/{escape(item["image"])}" target="_blank" rel="noopener" aria-label="Open full {name} editor screenshot">
          <img src="../../media/{escape(item["image"])}" width="{item["width"]}" height="{item["height"]}" decoding="async" fetchpriority="high" alt="ArSonMix {name} effect editor, {cat}">
        </a>
        <figcaption>{escape(item["origin"])}</figcaption>
      </figure>
    </section>
    <section class="detail-guidance" aria-labelledby="detail-guide">
      <div><span class="detail-kicker">HOW TO EXPLORE</span><h2 id="detail-guide">Understand the controls.</h2><p>See where {name} fits into the ArSonMix live mixing workflow before choosing an audio interface and trying an effect.</p></div>
      <ol>{details}</ol>
    </section>
    {gallery}
    <p class="detail-evidence">{escape(note)}</p>
    <section class="detail-more" aria-labelledby="related-effects"><div><span class="detail-kicker">CONTINUE DISCOVERING</span><h2 id="related-effects">More built-in audio effects</h2></div><div class="detail-links">{related}</div></section>
  </main>
  <footer class="detail-footer"><div class="detail-wrap"><span>ArSonMix — Free, open-source Windows digital mixer</span><a href="../../">Home</a><a href="../../#download">Verified downloads</a><a href="../../LICENSE.txt">AGPL-3.0 license</a></div></footer>
</body>
</html>
'''


def build_fx_pages(destination: Path, public_url: str):
    for item in FX_PAGES:
        target = destination / "effects" / item["slug"] / "index.html"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(page_html(item, public_url), encoding="utf-8")
