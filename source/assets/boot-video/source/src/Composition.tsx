import {Video} from "@remotion/media";
import {AbsoluteFill, Composition, staticFile, useVideoConfig} from "remotion";

// The user's exact 4-second input is the only visual layer. It is scaled to fit
// the composition at its native 16:9 ratio; no crop, retiming, overlays or fades.
const UserOpening = () => {
  const {fps} = useVideoConfig();
  return (
    <AbsoluteFill>
      <Video
        src={staticFile("source-opening.mp4")}
        premountFor={fps}
        muted
        objectFit="contain"
        style={{width: "100%", height: "100%"}}
      />
    </AbsoluteFill>
  );
};

export const MyComposition = () => (
  <>
    <Composition
      id="EkkoOpeningOriginal2K60"
      component={UserOpening}
      durationInFrames={240}
      fps={60}
      width={2560}
      height={1440}
    />
    <Composition
      id="EkkoOpeningLCD"
      component={UserOpening}
      durationInFrames={60}
      fps={15}
      width={240}
      // LCD exports only PNG/JPEG frames, never an H264/H265 video.
      // eslint-disable-next-line @remotion/even-dimensions
      height={135}
    />
  </>
);
