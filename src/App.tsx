import { useEffect, useState } from "react";
import { Toaster } from "sonner";
import { Home } from "@/components/Home";
import { SetupWizard } from "@/components/SetupWizard";
import { useRemote } from "@/hooks/useRemote";
import { api } from "@/lib/tauri";

export default function App() {
  const [ready, setReady] = useState(false);
  const [setupDone, setSetupDone] = useState(false);

  useEffect(() => {
    void api
      .getSetupState()
      .then((s) => {
        setSetupDone(s.completed);
        setReady(true);
      })
      .catch(() => setReady(true));
  }, []);

  useRemote(ready);

  if (!ready) {
    return <div className="flex h-full items-center justify-center bg-background" />;
  }

  return (
    <>
      <Toaster theme="dark" position="top-center" richColors />
      {setupDone ? <Home /> : <SetupWizard onComplete={() => setSetupDone(true)} />}
    </>
  );
}
