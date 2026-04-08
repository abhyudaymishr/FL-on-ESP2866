from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from xml.sax.saxutils import escape
from zipfile import ZIP_DEFLATED, ZipFile

P_NS = "http://schemas.openxmlformats.org/presentationml/2006/main"
A_NS = "http://schemas.openxmlformats.org/drawingml/2006/main"
R_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
REL_NS = "http://schemas.openxmlformats.org/package/2006/relationships"

SLIDE_WIDTH = 9144000
SLIDE_HEIGHT = 6858000
DEFAULT_AUTHOR = "OpenAI Codex"
DEFAULT_TITLE = "Physics-Informed Federated Learning on ESP8266"


@dataclass(frozen=True)
class SlideSpec:
    title: str
    lines: tuple[str, ...]
    accent: str = "79D4FF"
    subtitle: str | None = None


def default_slides() -> list[SlideSpec]:
    return [
        SlideSpec(
            title="Physics-Informed Federated Learning on ESP8266",
            subtitle="Research-grade thermal field reconstruction on constrained edge devices",
            lines=(
                "PDE solver + local learning + federated aggregation + live visualisation",
                "Custom binary protocol and simulation-backed validation pipeline",
                "Presentation generated directly from the repo without external PPT libraries",
            ),
            accent="79D4FF",
        ),
        SlideSpec(
            title="Mathematical Core",
            lines=(
                "dT/dt = alpha * Laplacian(T) + Q(x, t)",
                "Discrete step: T_next = T + dt * (alpha * lap + w1*T + w2*T^2 + b)",
                "The correction term models sensor bias, unknown heat sources, and environmental noise",
            ),
            accent="FFC66B",
        ),
        SlideSpec(
            title="Node Firmware",
            lines=(
                "Each ESP8266 stores current temperature, neighbor observations, alpha, and 3 trainable parameters",
                "The loop reads the sensor, computes the laplacian, predicts one step ahead, and updates by error",
                "Temperature packets are frequent, model packets are periodic, and aggregation is node-selectable",
            ),
            accent="8BF1C9",
        ),
        SlideSpec(
            title="Custom Protocol",
            lines=(
                "Header: NodeID (1 byte) | Type (1 byte) | Payload",
                "Packet types: 0x01 temperature, 0x02 model update, 0x03 global aggregation",
                "Payloads use fixed-point encoding to keep transport lightweight on ESP8266 hardware",
            ),
            accent="FF8F8F",
        ),
        SlideSpec(
            title="Federated Learning Logic",
            lines=(
                "Local loss: (T_measured - T_predicted)^2 with manual SGD updates for w1, w2, and b",
                "No TensorFlow Lite or autograd path is required for the MVP",
                "The aggregator weights peer models by laplacian magnitude before broadcasting a global model",
            ),
            accent="A8B8FF",
        ),
        SlideSpec(
            title="Tooling and Validation",
            lines=(
                "The visualisation module listens to live UDP packets and renders node + global model state",
                "The simulation integration test injects synthetic packets and verifies the dashboard API end to end",
                "PlatformIO builds the firmware for NodeMCU / ESP8266 within current memory limits",
            ),
            accent="79D4FF",
        ),
        SlideSpec(
            title="How To Use",
            lines=(
                "Firmware build: pio run",
                "Dashboard: python3 visualization/server.py --http-port 8080 --udp-port 4210",
                "Simulation test: python3 -m unittest tests/test_simulation_run.py",
                "Deck export: python3 -m ppt.generator --output artifacts/pifl_overview.pptx",
            ),
            accent="FFC66B",
        ),
    ]


def xml_header() -> str:
    return '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def paragraph_xml(text: str, size: int, color: str, bold: bool = False) -> str:
    bold_attr = ' b="1"' if bold else ""
    safe_text = escape(text)
    return (
        f"<a:p>"
        f"<a:r>"
        f"<a:rPr lang=\"en-US\" sz=\"{size}\"{bold_attr}>"
        f"<a:solidFill><a:srgbClr val=\"{color}\"/></a:solidFill>"
        f"<a:latin typeface=\"Aptos\"/>"
        f"</a:rPr>"
        f"<a:t>{safe_text}</a:t>"
        f"</a:r>"
        f"<a:endParaRPr lang=\"en-US\" sz=\"{size}\"/>"
        f"</a:p>"
    )


def textbox_xml(
    shape_id: int,
    name: str,
    x: int,
    y: int,
    cx: int,
    cy: int,
    paragraphs: list[str],
) -> str:
    return (
        f"<p:sp>"
        f"<p:nvSpPr>"
        f"<p:cNvPr id=\"{shape_id}\" name=\"{escape(name)}\"/>"
        f"<p:cNvSpPr txBox=\"1\"/>"
        f"<p:nvPr/>"
        f"</p:nvSpPr>"
        f"<p:spPr>"
        f"<a:xfrm><a:off x=\"{x}\" y=\"{y}\"/><a:ext cx=\"{cx}\" cy=\"{cy}\"/></a:xfrm>"
        f"<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
        f"<a:noFill/>"
        f"<a:ln><a:noFill/></a:ln>"
        f"</p:spPr>"
        f"<p:txBody>"
        f"<a:bodyPr wrap=\"square\" anchor=\"t\"/>"
        f"<a:lstStyle/>"
        f"{''.join(paragraphs)}"
        f"</p:txBody>"
        f"</p:sp>"
    )


def rect_xml(shape_id: int, name: str, x: int, y: int, cx: int, cy: int, fill: str) -> str:
    return (
        f"<p:sp>"
        f"<p:nvSpPr>"
        f"<p:cNvPr id=\"{shape_id}\" name=\"{escape(name)}\"/>"
        f"<p:cNvSpPr/>"
        f"<p:nvPr/>"
        f"</p:nvSpPr>"
        f"<p:spPr>"
        f"<a:xfrm><a:off x=\"{x}\" y=\"{y}\"/><a:ext cx=\"{cx}\" cy=\"{cy}\"/></a:xfrm>"
        f"<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
        f"<a:solidFill><a:srgbClr val=\"{fill}\"/></a:solidFill>"
        f"<a:ln><a:noFill/></a:ln>"
        f"</p:spPr>"
        f"<p:txBody><a:bodyPr/><a:lstStyle/><a:p/></p:txBody>"
        f"</p:sp>"
    )


def slide_xml(slide: SlideSpec, index: int, total: int) -> str:
    title_paragraphs = [paragraph_xml(slide.title, 2800, "EEF6FF", bold=True)]
    subtitle_paragraphs: list[str] = []
    if slide.subtitle:
        subtitle_paragraphs.append(paragraph_xml(slide.subtitle, 1500, "9BB4C7"))
    body_paragraphs = [paragraph_xml(f"• {line}", 1500, "D8E4F0") for line in slide.lines]
    footer_paragraphs = [paragraph_xml(f"Slide {index} of {total}", 1100, "9BB4C7")]

    shapes = [
        rect_xml(2, "Accent Bar", 0, 0, SLIDE_WIDTH, 342900, slide.accent),
        textbox_xml(3, "Title", 548640, 525780, 8046720, 914400, title_paragraphs),
    ]

    current_shape_id = 4
    if subtitle_paragraphs:
        shapes.append(textbox_xml(current_shape_id, "Subtitle", 548640, 1325880, 8046720, 457200, subtitle_paragraphs))
        current_shape_id += 1

    shapes.append(textbox_xml(current_shape_id, "Body", 640080, 1874520, 7863840, 3840480, body_paragraphs))
    current_shape_id += 1
    shapes.append(textbox_xml(current_shape_id, "Footer", 7315200, 6172200, 1188720, 274320, footer_paragraphs))

    return (
        f"{xml_header()}"
        f"<p:sld xmlns:a=\"{A_NS}\" xmlns:r=\"{R_NS}\" xmlns:p=\"{P_NS}\">"
        f"<p:cSld>"
        f"<p:bg>"
        f"<p:bgPr>"
        f"<a:solidFill><a:srgbClr val=\"08121D\"/></a:solidFill>"
        f"<a:effectLst/>"
        f"</p:bgPr>"
        f"</p:bg>"
        f"<p:spTree>"
        f"<p:nvGrpSpPr>"
        f"<p:cNvPr id=\"1\" name=\"\"/>"
        f"<p:cNvGrpSpPr/>"
        f"<p:nvPr/>"
        f"</p:nvGrpSpPr>"
        f"<p:grpSpPr>"
        f"<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm>"
        f"</p:grpSpPr>"
        f"{''.join(shapes)}"
        f"</p:spTree>"
        f"</p:cSld>"
        f"<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
        f"</p:sld>"
    )


def slide_rels_xml() -> str:
    return (
        f"{xml_header()}"
        f"<Relationships xmlns=\"{REL_NS}\">"
        f"<Relationship Id=\"rId1\" "
        f"Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
        f"Target=\"../slideLayouts/slideLayout1.xml\"/>"
        f"</Relationships>"
    )


def slide_layout_xml() -> str:
    return (
        f"{xml_header()}"
        f"<p:sldLayout xmlns:a=\"{A_NS}\" xmlns:r=\"{R_NS}\" xmlns:p=\"{P_NS}\" type=\"blank\" preserve=\"1\">"
        f"<p:cSld name=\"Blank\">"
        f"<p:spTree>"
        f"<p:nvGrpSpPr>"
        f"<p:cNvPr id=\"1\" name=\"\"/>"
        f"<p:cNvGrpSpPr/>"
        f"<p:nvPr/>"
        f"</p:nvGrpSpPr>"
        f"<p:grpSpPr>"
        f"<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm>"
        f"</p:grpSpPr>"
        f"</p:spTree>"
        f"</p:cSld>"
        f"<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
        f"</p:sldLayout>"
    )


def slide_layout_rels_xml() -> str:
    return (
        f"{xml_header()}"
        f"<Relationships xmlns=\"{REL_NS}\">"
        f"<Relationship Id=\"rId1\" "
        f"Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" "
        f"Target=\"../slideMasters/slideMaster1.xml\"/>"
        f"</Relationships>"
    )


def slide_master_xml() -> str:
    return (
        f"{xml_header()}"
        f"<p:sldMaster xmlns:a=\"{A_NS}\" xmlns:r=\"{R_NS}\" xmlns:p=\"{P_NS}\">"
        f"<p:cSld name=\"Master\">"
        f"<p:bg>"
        f"<p:bgPr>"
        f"<a:solidFill><a:srgbClr val=\"08121D\"/></a:solidFill>"
        f"<a:effectLst/>"
        f"</p:bgPr>"
        f"</p:bg>"
        f"<p:spTree>"
        f"<p:nvGrpSpPr>"
        f"<p:cNvPr id=\"1\" name=\"\"/>"
        f"<p:cNvGrpSpPr/>"
        f"<p:nvPr/>"
        f"</p:nvGrpSpPr>"
        f"<p:grpSpPr>"
        f"<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm>"
        f"</p:grpSpPr>"
        f"</p:spTree>"
        f"</p:cSld>"
        f"<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
        f"accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
        f"hlink=\"hlink\" folHlink=\"folHlink\"/>"
        f"<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
        f"<p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles>"
        f"</p:sldMaster>"
    )


def slide_master_rels_xml() -> str:
    return (
        f"{xml_header()}"
        f"<Relationships xmlns=\"{REL_NS}\">"
        f"<Relationship Id=\"rId1\" "
        f"Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
        f"Target=\"../slideLayouts/slideLayout1.xml\"/>"
        f"<Relationship Id=\"rId2\" "
        f"Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
        f"Target=\"../theme/theme1.xml\"/>"
        f"</Relationships>"
    )


def theme_xml() -> str:
    return (
        f"{xml_header()}"
        f"<a:theme xmlns:a=\"{A_NS}\" name=\"PIFL Theme\">"
        f"<a:themeElements>"
        f"<a:clrScheme name=\"PIFL\">"
        f"<a:dk1><a:srgbClr val=\"08121D\"/></a:dk1>"
        f"<a:lt1><a:srgbClr val=\"F5FBFF\"/></a:lt1>"
        f"<a:dk2><a:srgbClr val=\"15324A\"/></a:dk2>"
        f"<a:lt2><a:srgbClr val=\"D8E4F0\"/></a:lt2>"
        f"<a:accent1><a:srgbClr val=\"79D4FF\"/></a:accent1>"
        f"<a:accent2><a:srgbClr val=\"FFC66B\"/></a:accent2>"
        f"<a:accent3><a:srgbClr val=\"8BF1C9\"/></a:accent3>"
        f"<a:accent4><a:srgbClr val=\"FF8F8F\"/></a:accent4>"
        f"<a:accent5><a:srgbClr val=\"A8B8FF\"/></a:accent5>"
        f"<a:accent6><a:srgbClr val=\"9BB4C7\"/></a:accent6>"
        f"<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>"
        f"<a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>"
        f"</a:clrScheme>"
        f"<a:fontScheme name=\"PIFL\">"
        f"<a:majorFont><a:latin typeface=\"Aptos Display\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
        f"<a:minorFont><a:latin typeface=\"Aptos\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
        f"</a:fontScheme>"
        f"<a:fmtScheme name=\"PIFL\">"
        f"<a:fillStyleLst>"
        f"<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        f"<a:gradFill rotWithShape=\"1\">"
        f"<a:gsLst>"
        f"<a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:tint val=\"50000\"/><a:satMod val=\"300000\"/></a:schemeClr></a:gs>"
        f"<a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:shade val=\"50000\"/><a:satMod val=\"300000\"/></a:schemeClr></a:gs>"
        f"</a:gsLst>"
        f"<a:lin ang=\"5400000\" scaled=\"0\"/>"
        f"</a:gradFill>"
        f"<a:gradFill rotWithShape=\"1\">"
        f"<a:gsLst>"
        f"<a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:tint val=\"100000\"/><a:shade val=\"100000\"/><a:satMod val=\"130000\"/></a:schemeClr></a:gs>"
        f"<a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:tint val=\"50000\"/><a:shade val=\"100000\"/><a:satMod val=\"350000\"/></a:schemeClr></a:gs>"
        f"</a:gsLst>"
        f"<a:lin ang=\"5400000\" scaled=\"0\"/>"
        f"</a:gradFill>"
        f"</a:fillStyleLst>"
        f"<a:lnStyleLst>"
        f"<a:ln w=\"9525\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        f"<a:ln w=\"25400\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        f"<a:ln w=\"38100\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        f"</a:lnStyleLst>"
        f"<a:effectStyleLst>"
        f"<a:effectStyle><a:effectLst/></a:effectStyle>"
        f"<a:effectStyle><a:effectLst/></a:effectStyle>"
        f"<a:effectStyle><a:effectLst/></a:effectStyle>"
        f"</a:effectStyleLst>"
        f"<a:bgFillStyleLst>"
        f"<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        f"<a:solidFill><a:schemeClr val=\"phClr\"><a:tint val=\"95000\"/><a:satMod val=\"170000\"/></a:schemeClr></a:solidFill>"
        f"<a:gradFill rotWithShape=\"1\">"
        f"<a:gsLst>"
        f"<a:gs pos=\"0\"><a:schemeClr val=\"phClr\"><a:tint val=\"93000\"/><a:satMod val=\"150000\"/></a:schemeClr></a:gs>"
        f"<a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"><a:tint val=\"98000\"/><a:satMod val=\"130000\"/></a:schemeClr></a:gs>"
        f"</a:gsLst>"
        f"<a:path path=\"circle\"><a:fillToRect l=\"50000\" t=\"50000\" r=\"50000\" b=\"50000\"/></a:path>"
        f"</a:gradFill>"
        f"</a:bgFillStyleLst>"
        f"</a:fmtScheme>"
        f"</a:themeElements>"
        f"<a:objectDefaults/>"
        f"<a:extraClrSchemeLst/>"
        f"</a:theme>"
    )


def content_types_xml(slides: list[SlideSpec]) -> str:
    slide_overrides = "".join(
        f"<Override PartName=\"/ppt/slides/slide{index}.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>"
        for index in range(1, len(slides) + 1)
    )
    return (
        f"{xml_header()}"
        f"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        f"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        f"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        f"<Override PartName=\"/docProps/app.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
        f"<Override PartName=\"/docProps/core.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        f"<Override PartName=\"/ppt/presentation.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
        f"<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
        f"<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
        f"<Override PartName=\"/ppt/theme/theme1.xml\" "
        f"ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        f"{slide_overrides}"
        f"</Types>"
    )


def root_rels_xml() -> str:
    return (
        f"{xml_header()}"
        f"<Relationships xmlns=\"{REL_NS}\">"
        f"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
        f"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
        f"<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
        f"</Relationships>"
    )


def presentation_xml(slides: list[SlideSpec]) -> str:
    slide_entries = "".join(
        f"<p:sldId id=\"{255 + index}\" r:id=\"rId{index + 1}\"/>"
        for index in range(1, len(slides) + 1)
    )
    return (
        f"{xml_header()}"
        f"<p:presentation xmlns:a=\"{A_NS}\" xmlns:r=\"{R_NS}\" xmlns:p=\"{P_NS}\">"
        f"<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
        f"<p:sldIdLst>{slide_entries}</p:sldIdLst>"
        f"<p:sldSz cx=\"{SLIDE_WIDTH}\" cy=\"{SLIDE_HEIGHT}\"/>"
        f"<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
        f"<p:defaultTextStyle/>"
        f"</p:presentation>"
    )


def presentation_rels_xml(slides: list[SlideSpec]) -> str:
    slide_rels = "".join(
        f"<Relationship Id=\"rId{index + 1}\" "
        f"Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
        f"Target=\"slides/slide{index}.xml\"/>"
        for index in range(1, len(slides) + 1)
    )
    return (
        f"{xml_header()}"
        f"<Relationships xmlns=\"{REL_NS}\">"
        f"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>"
        f"{slide_rels}"
        f"</Relationships>"
    )


def app_xml(slides: list[SlideSpec]) -> str:
    titles = "".join(f"<vt:lpstr>{escape(slide.title)}</vt:lpstr>" for slide in slides)
    return (
        f"{xml_header()}"
        f"<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
        f"xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
        f"<Application>Codex PPT Module</Application>"
        f"<PresentationFormat>On-screen Show (4:3)</PresentationFormat>"
        f"<Slides>{len(slides)}</Slides>"
        f"<Notes>0</Notes>"
        f"<HiddenSlides>0</HiddenSlides>"
        f"<MMClips>0</MMClips>"
        f"<ScaleCrop>false</ScaleCrop>"
        f"<HeadingPairs><vt:vector size=\"2\" baseType=\"variant\">"
        f"<vt:variant><vt:lpstr>Slides</vt:lpstr></vt:variant>"
        f"<vt:variant><vt:i4>{len(slides)}</vt:i4></vt:variant>"
        f"</vt:vector></HeadingPairs>"
        f"<TitlesOfParts><vt:vector size=\"{len(slides)}\" baseType=\"lpstr\">{titles}</vt:vector></TitlesOfParts>"
        f"<Company>OpenAI</Company>"
        f"<LinksUpToDate>false</LinksUpToDate>"
        f"<SharedDoc>false</SharedDoc>"
        f"<HyperlinksChanged>false</HyperlinksChanged>"
        f"<AppVersion>16.0000</AppVersion>"
        f"</Properties>"
    )


def core_xml(deck_title: str, author: str) -> str:
    now = timestamp()
    return (
        f"{xml_header()}"
        f"<cp:coreProperties "
        f"xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        f"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        f"xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        f"xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" "
        f"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
        f"<dc:title>{escape(deck_title)}</dc:title>"
        f"<dc:subject>Project overview deck</dc:subject>"
        f"<dc:creator>{escape(author)}</dc:creator>"
        f"<cp:lastModifiedBy>{escape(author)}</cp:lastModifiedBy>"
        f"<dcterms:created xsi:type=\"dcterms:W3CDTF\">{now}</dcterms:created>"
        f"<dcterms:modified xsi:type=\"dcterms:W3CDTF\">{now}</dcterms:modified>"
        f"</cp:coreProperties>"
    )


def generate_project_deck(
    output_path: str | Path,
    slides: list[SlideSpec] | None = None,
    deck_title: str = DEFAULT_TITLE,
    author: str = DEFAULT_AUTHOR,
) -> Path:
    target = Path(output_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    slide_specs = slides if slides is not None else default_slides()

    with ZipFile(target, "w", compression=ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", content_types_xml(slide_specs))
        archive.writestr("_rels/.rels", root_rels_xml())
        archive.writestr("docProps/app.xml", app_xml(slide_specs))
        archive.writestr("docProps/core.xml", core_xml(deck_title, author))
        archive.writestr("ppt/presentation.xml", presentation_xml(slide_specs))
        archive.writestr("ppt/_rels/presentation.xml.rels", presentation_rels_xml(slide_specs))
        archive.writestr("ppt/slideLayouts/slideLayout1.xml", slide_layout_xml())
        archive.writestr("ppt/slideLayouts/_rels/slideLayout1.xml.rels", slide_layout_rels_xml())
        archive.writestr("ppt/slideMasters/slideMaster1.xml", slide_master_xml())
        archive.writestr("ppt/slideMasters/_rels/slideMaster1.xml.rels", slide_master_rels_xml())
        archive.writestr("ppt/theme/theme1.xml", theme_xml())

        total = len(slide_specs)
        for index, slide in enumerate(slide_specs, start=1):
            archive.writestr(f"ppt/slides/slide{index}.xml", slide_xml(slide, index, total))
            archive.writestr(f"ppt/slides/_rels/slide{index}.xml.rels", slide_rels_xml())

    return target


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a project PowerPoint deck for the PIFL ESP8266 MVP")
    parser.add_argument("--output", type=Path, default=Path("artifacts/pifl_overview.pptx"))
    parser.add_argument("--author", default=DEFAULT_AUTHOR)
    parser.add_argument("--title", default=DEFAULT_TITLE)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output = generate_project_deck(args.output, deck_title=args.title, author=args.author)
    print(output)


if __name__ == "__main__":
    main()
