using System;
using System.IO;
using System.Linq;
using System.Text;
class BarkSpu {
 static short[] Read(string path,out int rate){
  using var br=new BinaryReader(File.OpenRead(path));
  if(Encoding.ASCII.GetString(br.ReadBytes(4))!="RIFF")throw new Exception("Not RIFF");
  br.ReadInt32();if(Encoding.ASCII.GetString(br.ReadBytes(4))!="WAVE")throw new Exception("Not WAVE");
  rate=0;int channels=0,bits=0;byte[] data=Array.Empty<byte>();
  while(br.BaseStream.Position+8<=br.BaseStream.Length){
   string id=Encoding.ASCII.GetString(br.ReadBytes(4));int size=br.ReadInt32();long end=br.BaseStream.Position+size;
   if(id=="fmt "){int format=br.ReadInt16();channels=br.ReadInt16();rate=br.ReadInt32();br.ReadInt32();br.ReadInt16();bits=br.ReadInt16();if(format!=1||bits!=16)throw new Exception("Expected PCM16");}
   if(id=="data")data=br.ReadBytes(size);
   br.BaseStream.Position=Math.Min(br.BaseStream.Length,end+(size&1));
  }
  if(channels!=1||rate<=0||data.Length==0)throw new Exception("Expected mono PCM16");
  short[] result=new short[data.Length/2];Buffer.BlockCopy(data,0,result,0,data.Length);return result;
 }
 static void Main(string[] args){
  short[] pcm=Read(args[0],out int inputRate);const int outputRate=22050;
  int n=(int)Math.Ceiling(pcm.Length*(double)outputRate/inputRate);
  short[] down=new short[n];
  for(int i=0;i<n;i++){
   double src=i*(double)inputRate/outputRate;int lo=Math.Min(pcm.Length-1,(int)src),hi=Math.Min(pcm.Length-1,lo+1);
   down[i]=(short)Math.Clamp((int)Math.Round(pcm[lo]*(1-(src-lo))+pcm[hi]*(src-lo)),short.MinValue,short.MaxValue);
  }
  byte[] adpcm=SpuAdpcm.Encode(down);
  Directory.CreateDirectory(Path.GetDirectoryName(args[1])!);
  File.WriteAllBytes(args[1],adpcm);
  int blocks=adpcm.Length/16,invalid=0;
  for(int i=0;i<blocks;i++){int filter=adpcm[i*16]>>4,shift=adpcm[i*16]&15;if(filter>4||shift>12)invalid++;}
  if(invalid!=0)throw new Exception("Invalid SPU ADPCM block headers");
  string json=System.Text.Json.JsonSerializer.Serialize(new {
   source="user-supplied ROM-derived bark WAV",input_rate=inputRate,spu_rate=outputRate,pcm_samples=down.Length,
   adpcm_bytes=adpcm.Length,adpcm_blocks=blocks,invalid_headers=invalid,
   terminal_flag=adpcm[^15],output=args[1],
   note="Raw SPU ADPCM blocks generated with experimental SpuAdpcm.Encode; playback quality on real PS1 not yet verified."
  },new System.Text.Json.JsonSerializerOptions{WriteIndented=true});
  File.WriteAllText(args[2],json);Console.WriteLine(json);
 }
}


