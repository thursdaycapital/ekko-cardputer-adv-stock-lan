#include "OfficialStudioVoice.h"
#include "RamRecordingStore.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace official_voice;

struct Capture {
  std::vector<int16_t> samples;
  uint32_t rate = 0;
  bool cancel = false;
  static bool accept(void* context, const int16_t* data, size_t count, uint32_t rate) {
    auto& self = *static_cast<Capture*>(context);
    assert(count && count <= 1024 && !(count & 1));
    assert(rate >= 8000 && rate <= 96000);
    if (self.cancel) return false;
    self.rate = rate; self.samples.insert(self.samples.end(), data, data + count); return true;
  }
};

struct PatternReader : Reader {
  size_t size, offset = 0, maxRead = 0, fragment = 17;
  explicit PatternReader(size_t bytes) : size(bytes) {}
  size_t read(uint8_t* out, size_t capacity) override {
    const size_t count = std::min(std::min(capacity, fragment), size - offset);
    for (size_t i = 0; i < count; ++i) out[i] = uint8_t((offset + i) % 251);
    maxRead = std::max(maxRead, capacity); offset += count; return count;
  }
};

struct FakeTransport : Transport {
  int status = 200;
  std::string responseType = "text/plain; charset=utf-8", path, requestType, body;
  std::vector<uint8_t> response;
  size_t declaredLength = 0, calls = 0, uploadFragment = 257;
  bool post(const Request& request, Reader& source, ResponseSink& sink) override {
    ++calls;
    assert(std::strcmp(request.binding->baseUrl, "http://studio.test:8650") == 0);
    assert(std::strcmp(request.binding->userJwt, "test.header.signature") == 0);
    path = request.path; requestType = request.contentType; declaredLength = request.contentLength;
    uint8_t fragment[1024];
    assert(uploadFragment && uploadFragment <= sizeof(fragment));
    for (;;) { const size_t n = source.read(fragment, uploadFragment); if (!n) break; assert(n <= uploadFragment); body.append(reinterpret_cast<const char*>(fragment), n); }
    if (body.size() != declaredLength) return false;
    ResponseHeaders headers; headers.status = status; headers.contentType = responseType.c_str(); headers.contentLength = int64_t(response.size());
    if (!sink.begin(headers)) return false;
    for (size_t off = 0; off < response.size();) {
      const size_t n = std::min(size_t(31), response.size() - off);
      if (!sink.write(response.data() + off, n)) return false;
      off += n;
    }
    return true;
  }
};

std::vector<uint8_t> readFile(const char* path) {
  std::ifstream in(path, std::ios::binary); assert(in);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}
void put16(std::vector<uint8_t>& out, uint16_t value) { out.push_back(uint8_t(value)); out.push_back(uint8_t(value >> 8)); }
void put32(std::vector<uint8_t>& out, uint32_t value) { for (unsigned i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (i * 8))); }
void literal(std::vector<uint8_t>& out, const char* value) { out.insert(out.end(), value, value + std::strlen(value)); }
std::vector<uint8_t> wave(bool stereo = false) {
  std::vector<uint8_t> bytes;
  literal(bytes,"RIFF"); put32(bytes,0); literal(bytes,"WAVEJUNK"); put32(bytes,3);
  literal(bytes,"abc"); bytes.push_back(0); literal(bytes,"fmt "); put32(bytes,16); put16(bytes,1);
  put16(bytes,stereo?2:1); put32(bytes,16000); put32(bytes,16000*(stereo?4:2)); put16(bytes,stereo?4:2); put16(bytes,16);
  literal(bytes,"data"); put32(bytes,stereo?20:10);
  for (int16_t value : {int16_t(-3000),int16_t(4000),int16_t(-1000),int16_t(2500),int16_t(500)}) { put16(bytes,uint16_t(value)); if(stereo)put16(bytes,uint16_t(value+100)); }
  const uint32_t riff = uint32_t(bytes.size()-8); for(unsigned i=0;i<4;++i)bytes[4+i]=uint8_t(riff>>(i*8)); return bytes;
}
Result decode(const std::vector<uint8_t>& bytes, const char* mime, Capture& capture, size_t fragment = 1, RawPcmFormat raw = {}) {
  AudioDecoder decoder(Capture::accept,&capture); Result result; result.error=decoder.begin(mime,raw);
  if(!result.ok())return result;
  for(size_t offset=0;offset<bytes.size();) {const size_t count=std::min(fragment,bytes.size()-offset);result.error=decoder.write(bytes.data()+offset,count);if(!result.ok())return result;offset+=count;}
  return decoder.finish();
}

struct LimitedReader final : Reader {
  Reader& source;
  size_t fragment, consumed = 0, largestRead = 0;
  LimitedReader(Reader& reader, size_t limit) : source(reader), fragment(limit) { assert(limit); }
  size_t read(uint8_t* output, size_t capacity) override {
    const size_t requested = std::min(fragment, capacity);
    const size_t count = source.read(output, requested);
    assert(count <= requested);
    consumed += count; largestRead = std::max(largestRead, count);
    return count;
  }
};

static std::vector<uint8_t> readPcmSlices(Reader& reader) {
  const size_t slices[] = {1, 509, 2, 1024, 3, 17};
  uint8_t buffer[1024];
  std::vector<uint8_t> pcm;
  for (size_t part = 0;; ++part) {
    const size_t capacity = slices[part % (sizeof(slices) / sizeof(slices[0]))];
    const size_t count = reader.read(buffer, capacity);
    assert(count <= capacity);
    if (!count) break;
    pcm.insert(pcm.end(), buffer, buffer + count);
  }
  assert(reader.read(buffer, sizeof(buffer)) == 0);
  return pcm;
}

static uint16_t little16(const std::string& bytes, size_t at) {
  return uint16_t(uint8_t(bytes[at])) | uint16_t(uint16_t(uint8_t(bytes[at + 1])) << 8);
}
static uint32_t little32(const std::string& bytes, size_t at) {
  uint32_t value = 0;
  for (unsigned n = 0; n < 4; ++n) value |= uint32_t(uint8_t(bytes[at + n])) << (n * 8);
  return value;
}

// This oracle uses the separately consumed full RAM Reader, not the caller's
// Recording.pcmBytes: a compressed-byte count must fail even when HTTP and the
// generic streaming API can successfully carry a shorter, valid WAV.
static bool matchesRamUpload(const FakeTransport& transport, const std::vector<uint8_t>& pcm) {
  const std::string type = "multipart/form-data; boundary=";
  if (transport.requestType.compare(0, type.size(), type) != 0) return false;
  const std::string boundary = transport.requestType.substr(type.size());
  const std::string prefix = "--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  const std::string suffix = "\r\n--" + boundary + "--\r\n";
  const auto& body = transport.body;
  if (body.size() != transport.declaredLength || body.size() != prefix.size() + 44 + pcm.size() + suffix.size() ||
      body.compare(0, prefix.size(), prefix) || body.compare(body.size() - suffix.size(), suffix.size(), suffix)) return false;
  const size_t wave = prefix.size();
  return body.compare(wave, 4, "RIFF") == 0 && little32(body, wave + 4) == pcm.size() + 36 &&
      body.compare(wave + 8, 8, "WAVEfmt ") == 0 && little32(body, wave + 16) == 16 &&
      little16(body, wave + 20) == 1 && little16(body, wave + 22) == 1 &&
      little32(body, wave + 24) == 16000 && little32(body, wave + 28) == 32000 &&
      little16(body, wave + 32) == 2 && little16(body, wave + 34) == 16 &&
      body.compare(wave + 36, 4, "data") == 0 && little32(body, wave + 40) == pcm.size() &&
      std::memcmp(body.data() + wave + 44, pcm.data(), pcm.size()) == 0;
}

static void ramReaderUploadIntegration(const Binding& binding) {
  ram_recording::Store store;
  assert(store.prepare() == ram_recording::Error::Ok && store.begin() == ram_recording::Error::Ok);
  std::vector<uint8_t> input;
  for (size_t sample = 0; sample < ram_recording::kMaxPcmBytes / 2; ++sample)
    put16(input, uint16_t(int((sample * 811) % 65536) - 32768));
  for (size_t at = 0; at < input.size();) {
    const size_t bytes = std::min(ram_recording::kAppendBytes, input.size() - at);
    assert(store.append(input.data() + at, bytes) == ram_recording::Error::Ok); at += bytes;
  }
  assert(store.finish() == ram_recording::Error::Ok);
  assert(store.recordedBytes() == 160000 && store.encodedBytes() == 40000);
  ram_recording::Result error;
  std::vector<uint8_t> reference;
  {
    auto independent = store.openReader(error); assert(independent && error == ram_recording::Error::Ok);
    reference = readPcmSlices(*independent);
  }
  assert(reference.size() == store.recordedBytes() && reference != input && !store.readerActive());
  const size_t fragments[] = {1, 2, 3, 17, 257, 1024};
  size_t cases = 0;
  for (const size_t sourceFragment : fragments) for (const size_t uploadFragment : fragments) {
    auto reader = store.openReader(error); assert(reader && error == ram_recording::Error::Ok);
    LimitedReader source(*reader, sourceFragment);
    Recording recording; recording.pcm = &source; recording.pcmBytes = store.recordedBytes();
    recording.sampleRate = uint32_t(ram_recording::kSampleRate); recording.channels = 1;
    FakeTransport transport; transport.uploadFragment = uploadFragment; transport.response = {'o','k'};
    char transcript[8]{};
    const auto result = transcribe(transport, binding, recording, 0x123456789abcdef0ULL, transcript, sizeof(transcript));
    assert(result.ok() && std::string(transcript) == "ok" && transport.calls == 1);
    assert(source.consumed == reference.size() && source.largestRead <= sourceFragment);
    assert(matchesRamUpload(transport, reference));
    uint8_t extra; assert(reader->read(&extra, 1) == 0);
    reader.reset(); assert(!store.readerActive()); ++cases;
  }
  {
    auto reader = store.openReader(error); assert(reader && error == ram_recording::Error::Ok);
    LimitedReader source(*reader, 3);
    Recording wrong; wrong.pcm = &source; wrong.pcmBytes = store.encodedBytes();
    FakeTransport transport; transport.uploadFragment = 17; transport.response = {'o','k'};
    char transcript[8]{};
    const auto result = transcribe(transport, binding, wrong, 0x123456789abcdef0ULL, transcript, sizeof(transcript));
    assert(result.ok()); // A generic Reader has no independent total-length API.
    assert(source.consumed == 40000 && transport.body.size() == transport.declaredLength);
    assert(!matchesRamUpload(transport, reference)); // Reject the truncated-take regression.
    const auto remaining = readPcmSlices(*reader);
    assert(remaining.size() == 120000 && std::equal(remaining.begin(), remaining.end(), reference.begin() + 40000));
  }
  assert(!store.readerActive() && store.discard() == ram_recording::Error::Ok);
  std::cout << "RAM Reader -> official multipart: " << cases
            << " source/upload slice combinations passed; 160000B PCM / 40000B encoded; compressed-length negative caught\n";
}

int main(int argc,char** argv) {
  assert(argc==4 || argc==5);
  Binding binding; binding.baseUrl="http://studio.test:8650";binding.userJwt="test.header.signature";binding.profile="中文 profile";
  ramReaderUploadIntegration(binding);
  {
    PatternReader source(10*16000*2); Recording recording;recording.pcm=&source;recording.pcmBytes=source.size;
    FakeTransport transport;const std::string text="识别结果";transport.response.assign(text.begin(),text.end());char transcript[128];
    const Result r=transcribe(transport,binding,recording,0x123456789abcdef0ULL,transcript,sizeof(transcript));
    assert(r.ok()&&r.httpStatus==200&&r.textBytes==text.size()&&std::string(transcript)==text);
    assert(source.offset==source.size&&source.maxRead<=257);
    assert(transport.path=="/api/studio/voice/proxy/%E4%B8%AD%E6%96%87%20profile/v1/audio/transcriptions?profile=%E4%B8%AD%E6%96%87%20profile");
    assert(transport.requestType.find("multipart/form-data; boundary=----EkkoVoice") == 0);
    assert(transport.body.find("name=\"response_format\"\r\n\r\ntext\r\n")!=std::string::npos);
    const size_t riff=transport.body.find("RIFF");assert(riff!=std::string::npos);
    assert(transport.body.substr(riff+8,8)=="WAVEfmt ");assert(transport.body.substr(riff+36,4)=="data");
    for(size_t i=0;i<source.size;++i)assert(uint8_t(transport.body[riff+44+i])==uint8_t(i%251));
    assert(transport.body.size()==transport.declaredLength);
  }
  {
    PatternReader source(16);Recording rec;rec.pcm=&source;rec.pcmBytes=32;FakeTransport t;char output[8]={'x'};
    assert(transcribe(t,binding,rec,1,output,sizeof(output)).error==Error::SourceReadFailed&&output[0]==0);
  }
  for(int status:{401,403,409,500,302}) {
    PatternReader source(16);Recording rec;rec.pcm=&source;rec.pcmBytes=16;FakeTransport t;t.status=status;char output[32]={'x'};
    const Result r=transcribe(t,binding,rec,1,output,sizeof(output));
    assert(r.httpStatus==status&&r.error==(status==401||status==403?Error::AuthenticationRequired:Error::HttpFailed)&&output[0]==0);
  }
  {
    PatternReader source(16);Recording rec;rec.pcm=&source;rec.pcmBytes=16;FakeTransport t;t.response.assign(32,'x');char output[8];
    assert(transcribe(t,binding,rec,1,output,sizeof(output)).error==Error::ResponseTooLarge&&output[0]==0);
  }
  for(const char* base:{"https://user:pass@studio.test","http://studio.test/?token=x","http://studio.test/#evil","http://studio.test\r\nX: y","ftp://studio.test"}) {
    Binding bad=binding;bad.baseUrl=base;PatternReader source(16);Recording rec;rec.pcm=&source;rec.pcmBytes=16;FakeTransport t;char output[8];
    assert(transcribe(t,bad,rec,1,output,sizeof(output)).error==Error::InvalidBinding&&t.calls==0);
  }
  for(bool stereo:{false,true}) for(size_t fragment:{size_t(1),size_t(2),size_t(17),size_t(4096)}) {
    Capture c;const Result r=decode(wave(stereo),"Audio/WAV; codec=pcm",c,fragment);
    assert(r.ok()&&r.sampleRate==16000&&c.samples.size()==6);
    assert(c.samples[0]==(stereo?-2950:-3000));assert(c.samples[4]==(stereo?550:500));assert(c.samples[5]==0);
  }
  for(bool stereo:{false,true}) for(size_t fragment:{size_t(1),size_t(17),size_t(4096)}) {
    auto bytes=wave(stereo);
    const char dataTag[]="data";
    const auto tag=std::search(bytes.begin()+12,bytes.end(),dataTag,dataTag+4);
    assert(tag!=bytes.end());
    const size_t dataOffset=size_t(tag-bytes.begin());
    // Real non-seekable Studio TTS WAV: both sizes are UINT32_MAX.
    std::fill(bytes.begin()+4,bytes.begin()+8,0xff);
    Capture finite;assert(decode(bytes,"audio/wav",finite,fragment).ok());
    std::fill(bytes.begin()+dataOffset+4,bytes.begin()+dataOffset+8,0xff);
    Capture streaming;const auto result=decode(bytes,"audio/wav",streaming,fragment);
    assert(result.ok()&&result.sampleRate==16000&&streaming.samples==finite.samples);
    auto partial=bytes;partial.pop_back();Capture cut;
    assert(decode(partial,"audio/wav",cut,fragment).error==Error::InvalidAudio);
    auto bounded=bytes;const uint32_t riff=uint32_t(bounded.size()-8);
    for(unsigned i=0;i<4;++i)bounded[4+i]=uint8_t(riff>>(8*i));
    Capture contradictory;assert(decode(bounded,"audio/wav",contradictory,fragment).error==Error::InvalidAudio);
    auto unknownChunk=bytes;std::memcpy(unknownChunk.data()+dataOffset,"JUNK",4);
    Capture invalidChunk;assert(decode(unknownChunk,"audio/wav",invalidChunk,fragment).error==Error::InvalidAudio);
  }
  if(argc==5) {
    const auto recorded=readFile(argv[4]);
    for(size_t fragment:{size_t(1),size_t(257),size_t(4096)}) {
      Capture live;const auto result=decode(recorded,"audio/wav",live,fragment);
      assert(result.ok()&&result.samples>0&&result.sampleRate>0);
      std::cout<<"Supplied Studio WAV decoded: bytes="<<recorded.size()<<" samples="<<result.samples<<" rate="<<result.sampleRate<<" fragment="<<fragment<<"\n";
    }
  }
  {
    auto bytes=wave(true);
    const size_t fmt=24;assert(std::memcmp(bytes.data()+fmt,"fmt ",4)==0);
    bytes[fmt+4]=40;bytes[fmt+8]=0xfe;bytes[fmt+9]=0xff;
    const std::vector<uint8_t> extension={22,0,16,0,3,0,0,0,1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
    bytes.insert(bytes.begin()+fmt+24,extension.begin(),extension.end());
    const uint32_t riff=uint32_t(bytes.size()-8);for(unsigned i=0;i<4;++i)bytes[4+i]=uint8_t(riff>>(8*i));
    Capture c;assert(decode(bytes,"audio/vnd.wave",c,1).ok()&&c.samples[0]==-2950);
    bytes[fmt+8]=2;bytes[fmt+9]=0;Capture adpcm;assert(decode(bytes,"audio/wav",adpcm,1).error==Error::UnsupportedMedia&&adpcm.samples.empty());
  }
  {
    auto bytes=wave();bytes.pop_back();Capture c;assert(decode(bytes,"audio/wav",c).error==Error::InvalidAudio);
    Capture mime;assert(decode(bytes,"application/json",mime).error==Error::UnsupportedMedia&&mime.samples.empty());
    Capture unknown;assert(decode(std::vector<uint8_t>(16),"audio/pcm",unknown).error==Error::MissingAudioMetadata);
    Capture be;RawPcmFormat raw;raw.sampleRate=16000;raw.channels=1;assert(decode(std::vector<uint8_t>(16),"audio/L16",be,1,raw).error==Error::UnsupportedMedia);
    Capture pcm;assert(decode({0xe8,0x03,0x18,0xfc,0x32,0},"audio/x-pcm",pcm,1,raw).ok());assert((pcm.samples==std::vector<int16_t>{1000,-1000,50,0}));
    Capture partial;assert(decode({0,1,2},"audio/pcm",partial,1,raw).error==Error::InvalidAudio);
    Capture wrongEncoding;assert(decode({0,0,0,0},"audio/pcm;format=f32le",wrongEncoding,1,raw).error==Error::UnsupportedMedia&&wrongEncoding.samples.empty());
    Capture wrongRate;assert(decode({0,0,0,0},"audio/pcm;rate=24000",wrongRate,1,raw).error==Error::UnsupportedMedia);
    Capture correct;assert(decode({0,0,0,0},"audio/pcm;format=s16le;rate=16000;channels=1",correct,1,raw).ok());
    Capture cancelled;cancelled.cancel=true;assert(decode(wave(),"audio/wav",cancelled).error==Error::Cancelled);
  }
  const auto mp3=readFile(argv[1]);
  for(size_t fragment:{size_t(1),size_t(7),size_t(511),size_t(8192)}) {
    Capture c;const Result r=decode(mp3,"audio/mpeg",c,fragment);
    assert(r.ok()&&r.sampleRate==16000&&r.samples>=16000&&r.samples<20000);
    double energy=0;for(const auto sample:c.samples)energy+=double(sample)*sample;energy/=double(c.samples.size());
    assert(energy>1000000&&energy<15000000);
  }
  for(int fixture=2;fixture<=3;++fixture) {
    const auto mono=readFile(argv[fixture]);Capture c;const Result r=decode(mono,"audio/mp3",c,17);
    const uint32_t rate=fixture==2?44100:8000;
    assert(r.ok()&&r.sampleRate==rate&&r.samples>=rate&&r.samples<rate+4000);
    double energy=0;for(const auto sample:c.samples)energy+=double(sample)*sample;
    assert(energy/c.samples.size()>1000000);
  }
  {
    std::vector<uint8_t> tagged={'I','D','3',4,0,0,0,0,0,0};const size_t tagLength=200000;
    for(unsigned i=0;i<4;++i)tagged[6+i]=uint8_t((tagLength>>(7*(3-i)))&0x7f);
    tagged.resize(10+tagLength,0);tagged.insert(tagged.end(),mp3.begin(),mp3.end());Capture c;
    assert(decode(tagged,"audio/mpeg",c,31).ok()&&c.samples.size()>=16000);
    Capture garbage;assert(decode(std::vector<uint8_t>(1000,'x'),"audio/mpeg",garbage,11).error==Error::InvalidAudio&&garbage.samples.empty());
    Capture truncated;auto shortMp3=mp3;shortMp3.resize(11);assert(decode(shortMp3,"audio/mpeg",truncated).error==Error::InvalidAudio);
  }
  {
    FakeTransport t;t.responseType="audio/wav";t.response=wave(true);Capture c;
    const Result r=synthesize(t,binding,"你好\n\"\\",Capture::accept,&c);
    assert(r.ok()&&r.httpStatus==200&&c.samples.size()==6);
    assert(t.path=="/api/studio/tts/synthesize?profile=%E4%B8%AD%E6%96%87%20profile");
    assert(t.body=="{\"text\":\"你好\\u000a\\\"\\\\\",\"options\":{\"format\":\"wav\"}}");
    assert(t.body.size()==t.declaredLength&&t.body.find("provider")==std::string::npos&&t.body.find("key")==std::string::npos);
    FakeTransport denied;denied.status=401;Capture noAudio;assert(synthesize(denied,binding,"你好",Capture::accept,&noAudio).error==Error::AuthenticationRequired&&noAudio.samples.empty());
    FakeTransport invalid;Capture unused;assert(synthesize(invalid,binding," \n",Capture::accept,&unused).error==Error::InvalidText&&invalid.calls==0);
    FakeTransport mp3Transport;mp3Transport.responseType="audio/mpeg";mp3Transport.response=mp3;Capture spoken;
    assert(synthesize(mp3Transport,binding,"你好",Capture::accept,&spoken).ok()&&spoken.samples.size()>=16000);
    assert(mp3Transport.body=="{\"text\":\"你好\",\"options\":{\"format\":\"wav\"}}"); // Edge ignores WAV and returns MP3.
    FakeTransport rawTransport;rawTransport.responseType="audio/x-pcm";rawTransport.response={0xe8,0x03,0x18,0xfc};Capture rejected;
    const auto missing=synthesize(rawTransport,binding,"你好",Capture::accept,&rejected);
    assert(missing.error==Error::MissingAudioMetadata&&missing.httpStatus==200&&rejected.samples.empty());
    FakeTransport explicitRaw;explicitRaw.responseType="audio/x-pcm;format=s16le;rate=24000;channels=1";
    explicitRaw.response=rawTransport.response;Capture accepted;RawPcmFormat contract;contract.sampleRate=24000;contract.channels=1;
    const auto known=synthesize(explicitRaw,binding,"你好",Capture::accept,&accepted,contract);
    assert(known.ok()&&known.sampleRate==24000&&(accepted.samples==std::vector<int16_t>{1000,-1000}));
    FakeTransport mismatch;mismatch.responseType="audio/x-pcm;rate=16000;channels=1";mismatch.response=rawTransport.response;Capture wrong;
    assert(synthesize(mismatch,binding,"你好",Capture::accept,&wrong,contract).error==Error::UnsupportedMedia&&wrong.samples.empty());
  }
  assert(AudioDecoder::workingMemoryBytes()<24*1024);
  std::cout<<"official voice HTTP + streaming MP3/WAV/PCM tests passed; decoder working heap "<<AudioDecoder::workingMemoryBytes()<<" bytes\n";
}
