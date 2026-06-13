const fileInput = document.getElementById("fileInput");
const output = document.getElementById("output");
const copyBtn = document.getElementById("copyBtn");

fileInput.addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (!file) return;

  try {
    const html = await convertDocx(file);
    output.value = html;
  } catch (err) {
    console.error(err);
    output.value = "変換失敗: " + err.message;
  }
});

copyBtn.addEventListener("click", async () => {
  await navigator.clipboard.writeText(output.value);
  alert("コピーしました");
});

async function convertDocx(file) {
  const buffer = await file.arrayBuffer();
  const zip = await JSZip.loadAsync(buffer);

  const xml = await zip.file("word/document.xml").async("string");

  return convertXmlToHtml(xml);
}

function convertXmlToHtml(xmlText) {
  const parser = new DOMParser();
  const xml = parser.parseFromString(xmlText, "application/xml");

  const paragraphs = xml.getElementsByTagName("w:p");
  let lines = [];

  for (const p of paragraphs) {
    const text = convertParagraph(p).trim();

    if (text) {
      lines.push(text);
    }
  }

  return lines.join("\n");
}

function convertParagraph(p) {
  let html = "";

  for (const run of p.children) {
    if (run.tagName !== "w:r") continue;

    const ruby = run.getElementsByTagName("w:ruby")[0];

    if (ruby) {
      html += convertRuby(ruby);
    } else {
      html += extractText(run);
    }
  }

  return html;
}

function convertRuby(rubyNode) {
  const rt = rubyNode.getElementsByTagName("w:rt")[0];
  const rb = rubyNode.getElementsByTagName("w:rubyBase")[0];

  const rubyText = rt
    ? [...rt.getElementsByTagName("w:t")].map((t) => t.textContent).join("")
    : "";

  const baseText = rb
    ? [...rb.getElementsByTagName("w:t")].map((t) => t.textContent).join("")
    : "";

  return `<ruby>${escapeHtml(baseText)}<rt>${escapeHtml(rubyText)}</rt></ruby>`;
}

function extractText(run) {
  return [...run.getElementsByTagName("w:t")]
    .map((t) => escapeHtml(t.textContent))
    .join("");
}

function escapeHtml(str) {
  return str
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}
