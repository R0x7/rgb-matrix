// cube-sender: feeds the LED cube from this PC over UDP.
//   - system stats twice a second  ("S cpu=.. ram=.. gpu=.. ...")
//   - a 32-band spectrum of whatever the PC is playing, ~47x a second
//     ("A" + 32 bytes). Only the band levels leave the machine, never audio.
//
// No dependencies: builds with the C# compiler that ships with Windows
// (see build.ps1), so it has to stay within C# 5.
//
// usage: cube-sender.exe [pi-address] [port] [--verbose]

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
class MMDeviceEnumerator { }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams,
                               [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    [PreserveSig] int OpenPropertyStore(int access, out IntPtr store);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
}

[ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioClient {
    [PreserveSig] int Initialize(int shareMode, int streamFlags, long bufferDuration,
                                 long periodicity, IntPtr format, IntPtr sessionGuid);
    [PreserveSig] int GetBufferSize(out uint frames);
    [PreserveSig] int GetStreamLatency(out long latency);
    [PreserveSig] int GetCurrentPadding(out uint padding);
    [PreserveSig] int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);
    [PreserveSig] int GetMixFormat(out IntPtr format);
    [PreserveSig] int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
    [PreserveSig] int Start();
    [PreserveSig] int Stop();
    [PreserveSig] int Reset();
    [PreserveSig] int SetEventHandle(IntPtr handle);
    [PreserveSig] int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
}

[ComImport, Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioCaptureClient {
    [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags,
                                out ulong devicePosition, out ulong qpcPosition);
    [PreserveSig] int ReleaseBuffer(uint frames);
    [PreserveSig] int GetNextPacketSize(out uint frames);
}

static class CubeSender {
    const int NBANDS = 32, FFT = 2048, HOP = 1024;

    static UdpClient udp;
    static IPEndPoint pi;
    static bool verbose;
    static volatile float gpu = 0, gpuTemp = 0, vram = 0;

    [StructLayout(LayoutKind.Sequential)]
    struct MEMORYSTATUSEX {
        public uint dwLength, dwMemoryLoad;
        public ulong ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile,
                     ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual;
    }
    [DllImport("kernel32.dll")] static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX m);

    static void Send(byte[] data) {
        try { udp.Send(data, data.Length, pi); } catch (SocketException) { }
    }

    [MTAThread]
    static int Main(string[] args) {
        string host = "192.168.50.66";
        int port = 7777, positional = 0;
        foreach (string a in args) {
            if (a == "--verbose") verbose = true;
            else if (positional++ == 0) host = a;
            else port = int.Parse(a);
        }
        AppDomain.CurrentDomain.UnhandledException += delegate(object s, UnhandledExceptionEventArgs e) {
            try {
                System.IO.File.AppendAllText(
                    System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "cube-sender.log"),
                    DateTime.Now + " crash: " + e.ExceptionObject + Environment.NewLine);
            } catch (Exception) { }
        };
        bool first;
        Mutex single = new Mutex(true, "ledcube-sender", out first);
        if (!first) return 0;  // already running

        udp = new UdpClient();
        pi = new IPEndPoint(Dns.GetHostAddresses(host)[0], port);

        Thread audio = new Thread(AudioLoop);
        audio.IsBackground = true;
        audio.Start();
        Thread gpuThread = new Thread(GpuLoop);
        gpuThread.IsBackground = true;
        gpuThread.Start();

        StatsLoop();
        GC.KeepAlive(single);
        return 0;
    }

    // ---------------------------------------------------------------- stats

    static void StatsLoop() {
        CultureInfo inv = CultureInfo.InvariantCulture;
        int ncores = Environment.ProcessorCount;
        PerformanceCounter total = new PerformanceCounter("Processor", "% Processor Time", "_Total");
        PerformanceCounter[] cores = new PerformanceCounter[ncores];
        for (int i = 0; i < ncores; i++)
            cores[i] = new PerformanceCounter("Processor", "% Processor Time", i.ToString(inv));
        List<PerformanceCounter> rx = new List<PerformanceCounter>(), tx = new List<PerformanceCounter>();
        try {
            foreach (string nic in new PerformanceCounterCategory("Network Interface").GetInstanceNames()) {
                rx.Add(new PerformanceCounter("Network Interface", "Bytes Received/sec", nic));
                tx.Add(new PerformanceCounter("Network Interface", "Bytes Sent/sec", nic));
            }
        } catch (Exception) { }

        for (;;) {
            Thread.Sleep(500);
            try {
                StringBuilder sb = new StringBuilder("S ");
                sb.AppendFormat(inv, "cpu={0:0.0}", total.NextValue());
                MEMORYSTATUSEX m = new MEMORYSTATUSEX();
                m.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
                if (GlobalMemoryStatusEx(ref m)) sb.AppendFormat(inv, " ram={0}", m.dwMemoryLoad);
                sb.AppendFormat(inv, " gpu={0:0} gput={1:0} vram={2:0}", gpu, gpuTemp, vram);
                float down = 0, up = 0;
                foreach (PerformanceCounter c in rx) down += c.NextValue();
                foreach (PerformanceCounter c in tx) up += c.NextValue();
                sb.AppendFormat(inv, " down={0:0.0} up={1:0.0}", down * 8e-6, up * 8e-6);  // Mbit/s
                sb.Append(" cores=");
                for (int i = 0; i < ncores; i++)
                    sb.AppendFormat(inv, i == 0 ? "{0:0}" : ",{0:0}", cores[i].NextValue());
                string line = sb.ToString();
                if (verbose) Console.WriteLine(line);
                Send(Encoding.ASCII.GetBytes(line));
            } catch (Exception e) {
                if (verbose) Console.WriteLine("stats: " + e.Message);
            }
        }
    }

    // NVIDIA only. One short nvidia-smi query every two seconds (rather than its
    // loop mode, which would leave an orphan behind if the sender is killed).
    static void GpuLoop() {
        CultureInfo inv = CultureInfo.InvariantCulture;
        for (;;) {
            try {
                ProcessStartInfo psi = new ProcessStartInfo("nvidia-smi",
                    "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total " +
                    "--format=csv,noheader,nounits");
                psi.UseShellExecute = false;
                psi.RedirectStandardOutput = true;
                psi.CreateNoWindow = true;
                using (Process p = Process.Start(psi)) {
                    string line = p.StandardOutput.ReadLine();
                    p.WaitForExit();
                    string[] f = (line ?? "").Split(',');
                    float u, t, used, tot;
                    if (f.Length >= 4) {
                        if (float.TryParse(f[0], NumberStyles.Float, inv, out u)) gpu = u;
                        if (float.TryParse(f[1], NumberStyles.Float, inv, out t)) gpuTemp = t;
                        if (float.TryParse(f[2], NumberStyles.Float, inv, out used) &&
                            float.TryParse(f[3], NumberStyles.Float, inv, out tot) && tot > 0)
                            vram = 100f * used / tot;
                    }
                }
            } catch (Exception) {
                return;  // no nvidia-smi: leave GPU at zero
            }
            Thread.Sleep(2000);
        }
    }

    // ---------------------------------------------------------------- audio

    static void AudioLoop() {
        for (;;) {
            try { Capture(); }
            catch (Exception e) { if (verbose) Console.WriteLine("audio: " + e.Message); }
            Thread.Sleep(2000);  // device unplugged / changed: start over
        }
    }

    static void Check(int hr, string what) {
        if (hr < 0) throw new Exception(what + " failed: 0x" + hr.ToString("X8"));
    }

    static string DefaultDevice(IMMDeviceEnumerator en, out IMMDevice dev) {
        Check(en.GetDefaultAudioEndpoint(0 /*render*/, 1 /*multimedia*/, out dev), "GetDefaultAudioEndpoint");
        string id;
        Check(dev.GetId(out id), "GetId");
        return id;
    }

    static void Capture() {
        IMMDeviceEnumerator en = (IMMDeviceEnumerator)new MMDeviceEnumerator();
        IMMDevice dev;
        string devId = DefaultDevice(en, out dev);

        Guid iidClient = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
        object o;
        Check(dev.Activate(ref iidClient, 23 /*CLSCTX_ALL*/, IntPtr.Zero, out o), "Activate");
        IAudioClient client = (IAudioClient)o;

        IntPtr fmt;
        Check(client.GetMixFormat(out fmt), "GetMixFormat");
        int channels = Marshal.ReadInt16(fmt, 2);
        int rate = Marshal.ReadInt32(fmt, 4);
        int bits = Marshal.ReadInt16(fmt, 14);
        if (bits != 32) throw new Exception("unexpected mix format (" + bits + " bit)");
        Check(client.Initialize(0 /*shared*/, 0x00020000 /*loopback*/, 2000000, 0, fmt, IntPtr.Zero),
              "Initialize");
        Marshal.FreeCoTaskMem(fmt);

        Guid iidCapture = new Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317");
        Check(client.GetService(ref iidCapture, out o), "GetService");
        IAudioCaptureClient cap = (IAudioCaptureClient)o;
        Check(client.Start(), "Start");
        if (verbose) Console.WriteLine("audio: capturing " + rate + " Hz, " + channels + " ch");

        // log-spaced band edges, 40 Hz .. 16 kHz, each at least one FFT bin wide
        int[] lo = new int[NBANDS], hi = new int[NBANDS];
        int prev = 1;
        for (int b = 0; b < NBANDS; b++) {
            double f1 = 40.0 * Math.Pow(400.0, (b + 1) / (double)NBANDS);
            lo[b] = prev;
            hi[b] = Math.Min(FFT / 2, Math.Max(lo[b] + 1, (int)(f1 * FFT / rate)));
            prev = hi[b];
        }
        float[] window = new float[FFT];
        for (int i = 0; i < FFT; i++)
            window[i] = 0.5f - 0.5f * (float)Math.Cos(2 * Math.PI * i / (FFT - 1));

        float[] ring = new float[FFT], chunk = new float[4096 * 8];
        float[] re = new float[FFT], im = new float[FFT];
        byte[] packet = new byte[1 + NBANDS];
        packet[0] = (byte)'A';
        int ringPos = 0, fresh = 0;
        double agc = -30;
        Stopwatch sinceCheck = Stopwatch.StartNew();

        for (;;) {
            Thread.Sleep(8);
            if (sinceCheck.ElapsedMilliseconds > 3000) {  // follow the default output device
                sinceCheck.Restart();
                IMMDevice d2;
                if (DefaultDevice(en, out d2) != devId) throw new Exception("default device changed");
                Marshal.ReleaseComObject(d2);
            }
            uint frames;
            Check(cap.GetNextPacketSize(out frames), "GetNextPacketSize");
            while (frames > 0) {
                IntPtr data;
                uint n, flags;
                ulong p1, p2;
                Check(cap.GetBuffer(out data, out n, out flags, out p1, out p2), "GetBuffer");
                int count = (int)n * channels;
                if (count > chunk.Length) chunk = new float[count];
                if ((flags & 2) != 0) Array.Clear(chunk, 0, count);  // silent buffer
                else Marshal.Copy(data, chunk, 0, count);
                Check(cap.ReleaseBuffer(n), "ReleaseBuffer");

                for (int i = 0; i < (int)n; i++) {
                    float s = 0;
                    for (int c = 0; c < channels && c < 2; c++) s += chunk[i * channels + c];
                    ring[ringPos] = s * 0.5f;
                    ringPos = (ringPos + 1) % FFT;
                    if (++fresh >= HOP) {
                        fresh = 0;
                        for (int k = 0; k < FFT; k++) {
                            re[k] = ring[(ringPos + k) % FFT] * window[k];
                            im[k] = 0;
                        }
                        Fft(re, im);
                        double loudest = -200;
                        double[] db = new double[NBANDS];
                        for (int b = 0; b < NBANDS; b++) {
                            float mx = 0;
                            for (int k = lo[b]; k < hi[b]; k++) {
                                float m = re[k] * re[k] + im[k] * im[k];
                                if (m > mx) mx = m;
                            }
                            // amplitude in dBFS, tilted so the treble is not lost under the bass
                            db[b] = 10 * Math.Log10(mx / ((FFT / 4.0) * (FFT / 4.0)) + 1e-12) + b * 0.45;
                            if (db[b] > loudest) loudest = db[b];
                        }
                        if (loudest < -70) continue;  // silence: send nothing, the cube moves on
                        agc = Math.Max(Math.Max(agc - 2.0 * HOP / rate, loudest), -45);
                        for (int b = 0; b < NBANDS; b++) {
                            double v = (db[b] - (agc - 48)) / 48;
                            packet[1 + b] = (byte)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255);
                        }
                        Send(packet);
                    }
                }
                Check(cap.GetNextPacketSize(out frames), "GetNextPacketSize");
            }
        }
    }

    static void Fft(float[] re, float[] im) {
        int n = re.Length;
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                float t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        for (int len = 2; len <= n; len <<= 1) {
            double ang = -2 * Math.PI / len;
            float wr = (float)Math.Cos(ang), wi = (float)Math.Sin(ang);
            for (int i = 0; i < n; i += len) {
                float cr = 1, ci = 0;
                for (int k = 0; k < len / 2; k++) {
                    int a = i + k, b = i + k + len / 2;
                    float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                    float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }
}
