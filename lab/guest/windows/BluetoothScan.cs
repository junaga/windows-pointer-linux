using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Windows.Devices.Bluetooth.Advertisement;

internal sealed class SeenDevice
{
    internal ulong Address;
    internal short Rssi;
    internal string Name = "";
    internal int Reports;
    internal string Manufacturers = "";
    internal string Services = "";
}

internal static class BluetoothScan
{
    private static readonly object Sync = new object();
    private static readonly Dictionary<ulong, SeenDevice> Devices =
        new Dictionary<ulong, SeenDevice>();

    private static void Received(
        BluetoothLEAdvertisementWatcher sender,
        BluetoothLEAdvertisementReceivedEventArgs args)
    {
        lock (Sync)
        {
            SeenDevice device;
            if (!Devices.TryGetValue(args.BluetoothAddress, out device))
            {
                device = new SeenDevice { Address = args.BluetoothAddress };
                Devices.Add(args.BluetoothAddress, device);
            }

            device.Rssi = args.RawSignalStrengthInDBm;
            device.Reports++;
            if (!String.IsNullOrEmpty(args.Advertisement.LocalName))
                device.Name = args.Advertisement.LocalName;
            device.Manufacturers = String.Join(
                ",",
                args.Advertisement.ManufacturerData
                    .Select(value => value.CompanyId.ToString("X4")));
            device.Services = String.Join(
                ",",
                args.Advertisement.ServiceUuids.Select(value => value.ToString()));
        }
    }

    public static int Main(string[] arguments)
    {
        int seconds = 12;
        if (arguments.Length > 0 && !Int32.TryParse(arguments[0], out seconds))
            return 2;

        var watcher = new BluetoothLEAdvertisementWatcher();
        watcher.ScanningMode = BluetoothLEScanningMode.Active;
        watcher.Received += Received;
        watcher.Start();
        Thread.Sleep(seconds * 1000);
        watcher.Stop();
        Thread.Sleep(500);
        watcher.Received -= Received;

        lock (Sync)
        {
            foreach (SeenDevice device in Devices.Values
                .OrderByDescending(value => value.Rssi))
            {
                Console.WriteLine(
                    "{0:X12}\t{1}\t{2}\t{3}\t{4}\t{5}",
                    device.Address,
                    device.Rssi,
                    device.Reports,
                    device.Name,
                    device.Manufacturers,
                    device.Services);
            }
        }
        return 0;
    }
}
