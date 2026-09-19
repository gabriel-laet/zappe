export function VoiceListen({ message }: { message: string }) {
  return (
    <div className="voice-listen" role="status" aria-live="polite">
      <span className="voice-dot" aria-hidden />
      <p className="tv-title">{message}</p>
    </div>
  );
}
