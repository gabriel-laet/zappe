import { useEffect, useState } from "react";

/** D-pad focus for a short list of couch actions (no typing). */
export function useTvChoice(length: number, onActivate: (index: number) => void) {
  const [index, setIndex] = useState(0);

  useEffect(() => {
    setIndex((i) => Math.min(i, Math.max(0, length - 1)));
  }, [length]);

  useEffect(() => {
    if (length < 1) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.code === "ArrowRight" || e.code === "ArrowDown") {
        e.preventDefault();
        setIndex((i) => (i + 1) % length);
      } else if (e.code === "ArrowLeft" || e.code === "ArrowUp") {
        e.preventDefault();
        setIndex((i) => (i - 1 + length) % length);
      } else if (e.code === "Enter" || e.code === "NumpadEnter") {
        e.preventDefault();
        onActivate(index);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [index, length, onActivate]);

  return index;
}
