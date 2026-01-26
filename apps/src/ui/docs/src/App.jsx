import { useEffect, useMemo, useRef, useState } from "react";
import { Marked } from "marked";
import mermaid from "mermaid";

const markdownModules = import.meta.glob("./screens/*.md", {
  eager: true,
  query: "?raw",
  import: "default"
});

const escapeHtml = (value) => {
  const text = typeof value === "string" ? value : String(value ?? "");
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
};

const normalizePlantUml = (value) =>
  String(value ?? "")
    .replace(/\r\n/g, "\n")
    .replace(/[ \t]+$/gm, "")
    .replace(/\n+$/, "");

const hashPlantUml = (value) => {
  const text = normalizePlantUml(value);
  let hash = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  for (let i = 0; i < text.length; i += 1) {
    hash ^= BigInt(text.charCodeAt(i));
    hash = (hash * prime) & 0xffffffffffffffffn;
  }
  return hash.toString(16).padStart(16, "0");
};

const formatTitle = (id) =>
  id
    .split("-")
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");

const plantumlAssetBase = import.meta.env.VITE_PLANTUML_ASSET_BASE ?? "";

const renderPlantUml = (code) => {
  const normalized = normalizePlantUml(code);
  const encoded = hashPlantUml(normalized);
  if (plantumlAssetBase) {
    const baseUrl = plantumlAssetBase.replace(/\/+$/, "");
    const src = `${baseUrl}/${encoded}.svg`;
    return `<img class="plantuml" alt="PlantUML diagram" src="${src}" />`;
  }
  const escaped = escapeHtml(normalized);
  return `<pre><code class="language-plantuml">${escaped}</code></pre>`;
};

const markdown = new Marked({
  renderer: {
    code(code, info) {
      const codeText =
        typeof code === "string"
          ? code
          : typeof code?.text === "string"
            ? code.text
            : String(code ?? "");
      const language = (
        typeof info === "string" ? info : code?.lang ?? ""
      )
        .trim()
        .toLowerCase();
      if (language === "mermaid") {
        return `<div class="mermaid">${escapeHtml(codeText)}</div>`;
      }
      if (language === "plantuml" || language === "puml" || language === "salt") {
        return renderPlantUml(codeText);
      }
      const escaped = escapeHtml(codeText);
      const className = language ? `language-${language}` : "";
      return `<pre><code class="${className}">${escaped}</code></pre>`;
    }
  }
});

const screens = Object.entries(markdownModules)
  .map(([path, content]) => {
    const id = path.replace("./screens/", "").replace(".md", "");
    return {
      id,
      title: formatTitle(id),
      content: typeof content === "string" ? content : ""
    };
  })
  .sort((a, b) => a.title.localeCompare(b.title));

const getScreenFromUrl = () => {
  if (typeof window === "undefined") {
    return "";
  }
  const params = new URLSearchParams(window.location.search);
  const screen = params.get("screen");
  if (!screen) {
    return "";
  }
  return screens.some((doc) => doc.id === screen) ? screen : "";
};

export default function App() {
  const [activeId, setActiveId] = useState(
    getScreenFromUrl() || screens[0]?.id || ""
  );
  const [mermaidReady, setMermaidReady] = useState(true);
  const containerRef = useRef(null);

  const activeDoc = useMemo(
    () => screens.find((doc) => doc.id === activeId),
    [activeId]
  );

  const renderedHtml = useMemo(() => {
    if (!activeDoc?.content) {
      return "<p>Select a screen to view its documentation.</p>";
    }
    return markdown.parse(activeDoc.content);
  }, [activeDoc]);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }
    const params = new URLSearchParams(window.location.search);
    if (activeId) {
      params.set("screen", activeId);
    } else {
      params.delete("screen");
    }
    const next = params.toString();
    const url = next ? `${window.location.pathname}?${next}` : window.location.pathname;
    window.history.replaceState({}, "", url);
  }, [activeId]);

  useEffect(() => {
    if (!containerRef.current) {
      return;
    }

    setMermaidReady(false);
    containerRef.current.dataset.mermaidReady = "false";

    mermaid.initialize({
      startOnLoad: false,
      theme: "neutral",
      themeVariables: {
        primaryColor: "#f4b26a",
        primaryTextColor: "#1d1b16",
        lineColor: "#3a3328",
        fontFamily: "Space Grotesk, ui-sans-serif, system-ui"
      }
    });

    const nodes = containerRef.current.querySelectorAll(".mermaid");
    if (nodes.length === 0) {
      setMermaidReady(true);
      containerRef.current.dataset.mermaidReady = "true";
      return;
    }

    mermaid
      .run({ nodes })
      .then(() => {
        setMermaidReady(true);
        if (containerRef.current) {
          containerRef.current.dataset.mermaidReady = "true";
        }
      })
      .catch((error) => {
        console.warn("Mermaid render failed:", error);
        setMermaidReady(true);
        if (containerRef.current) {
          containerRef.current.dataset.mermaidReady = "true";
        }
      });
  }, [renderedHtml]);

  return (
    <div className="app" data-docs-root>
      <header className="top-bar">
        <div className="brand">
          <div className="brand-mark">DS</div>
          <div>
            <div className="brand-title">DirtSim UI Docs</div>
            <div className="brand-subtitle">PlantUML-powered viewer</div>
          </div>
        </div>
      </header>
      <div className="layout">
        <aside className="sidebar">
          <div className="sidebar-title">Screens</div>
          <nav className="nav">
            {screens.length === 0 && (
              <div className="empty">No screens found in src/screens.</div>
            )}
            {screens.map((doc) => (
              <button
                key={doc.id}
                type="button"
                className={
                  doc.id === activeId ? "nav-item nav-item-active" : "nav-item"
                }
                onClick={() => setActiveId(doc.id)}
                data-screen-id={doc.id}
              >
                {doc.title}
              </button>
            ))}
          </nav>
        </aside>
        <main className="content" data-active-screen={activeDoc?.id ?? ""}>
          <div className="content-header">
            <h1>{activeDoc?.title ?? "Docs"}</h1>
          </div>
          <section
            className="content-body"
            ref={containerRef}
            data-mermaid-ready={mermaidReady ? "true" : "false"}
          >
            <div
              className="markdown-body"
              dangerouslySetInnerHTML={{ __html: renderedHtml }}
            />
          </section>
        </main>
      </div>
    </div>
  );
}
