# Bluetooth Classic Explained

English | [日本語](bluetooth_classic_jp.md)

This is background material about Bluetooth Classic, not a claim that this library supports every profile mentioned here. For current support and usage, read the [API reference](api_reference.md) and [Validation scope](validation.md). HFP is unsupported.

## What is Bluetooth Classic?

Bluetooth Classic, also known as Bluetooth Basic Rate/Enhanced Data Rate (BR/EDR), is the original version of Bluetooth technology designed for continuous, high-throughput data streaming. It's primarily used for applications that require a constant, robust connection and significant data transfer, such as:

*   **Wireless Audio Streaming (A2DP)**: Connecting headphones, speakers, or car audio systems.
*   **Voice Calls (HFP/HSP)**: Hands-free calling in cars or with headsets.
*   **Data Transfer (SPP)**: Legacy serial port emulation for devices like printers or older modems.
*   **Human Interface Devices (HID)**: Connecting keyboards, mice, and game controllers (though many newer HID devices now use BLE).

### Key Characteristics of Bluetooth Classic:

*   **Higher Data Throughput**: Generally offers higher data rates (up to 3 Mbps) compared to Bluetooth Low Energy (BLE).
*   **Streaming Capabilities**: Optimized for continuous data streams, making it ideal for audio.
*   **Connection-Oriented**: Establishes a dedicated, persistent connection between devices.
*   **Higher Power Consumption**: Typically consumes more power than BLE due to its continuous connection and higher data rates.
*   **Complex Pairing**: The pairing process can sometimes be more involved, often requiring PIN codes or confirmation.

## How does Bluetooth Classic work?

Bluetooth Classic operates in the 2.4 GHz ISM band, using a technology called Frequency Hopping Spread Spectrum (FHSS). This means it rapidly switches frequencies many times per second to avoid interference and enhance security.

When two Bluetooth Classic devices connect:

1.  **Inquiry**: A device looking for others sends out inquiry signals.
2.  **Paging**: Once a device is discovered, the initiator sends paging signals to establish a connection.
3.  **Connection**: A dedicated link is established, and devices can then exchange data. This connection is maintained as long as both devices are within range and powered on.

## Bluetooth Classic vs. Bluetooth Low Energy (BLE)

It's important to distinguish Bluetooth Classic from Bluetooth Low Energy (BLE), which is a separate technology introduced with Bluetooth 4.0.

| Feature             | Bluetooth Classic (BR/EDR)                               | Bluetooth Low Energy (BLE)                               |
| :------------------ | :------------------------------------------------------- | :------------------------------------------------------- |
| **Primary Use**     | Continuous data streaming (audio, voice)                  | Periodic data transfer (sensors, wearables, IoT)         |
| **Data Throughput** | Higher (up to 3 Mbps)                                    | Lower (up to ~2 Mbps, often less in practice)            |
| **Power Consumption** | Higher                                                   | Very Low                                                 |
| **Connection Type** | Connection-oriented (persistent link)                    | Connection-oriented or Connectionless (short bursts)     |
| **Topology**        | Point-to-point, limited scatternet                       | Point-to-point, broadcast, mesh                          |
| **Pairing**         | More complex (PIN, passkey)                              | Simpler (just works, passkey, OOB)                       |
| **Discovery**       | Inquiry/Paging                                           | Advertising/Scanning                                     |
| **Profiles**        | A2DP, HFP, SPP, HID, etc.                                | GATT-based profiles (e.g., Heart Rate Profile, Proximity Profile) |

## Why is Bluetooth Classic still relevant?

Despite the rise of BLE, Bluetooth Classic remains crucial for applications demanding high-quality, continuous data streams, especially in audio. For example, listening to music on wireless headphones or making hands-free calls in a car still heavily relies on Bluetooth Classic's A2DP and HFP profiles due to their ability to handle large amounts of continuous audio data reliably.

Understanding Bluetooth Classic is essential for working with devices that use these established profiles and for developing applications that leverage its unique strengths.
