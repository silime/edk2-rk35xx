// LSM6DS3TR-C accelerometer/gyroscope device node
Device(IMU0)
{
    Name(_HID, "SMO8B30")
    Name(_UID, 1)

    Method(_CRS, 0x0, NotSerialized)
    {
        Name(RBUF, ResourceTemplate()
        {          
            I2CSerialBus(0x6A, ControllerInitiated, 400000, AddressingMode7Bit, "\\_SB.I2C4", 0, ResourceConsumer) 
            GpioInt(Edge, ActiveHigh, Exclusive, PullDown, 0, "\\_SB.GPI4") 
            {
                GPIO_PIN_PC2
            }
        })
        Return(RBUF)
    } // Method (_CRS ...)

    Method(_DSM, 0x4, NotSerialized)
    {
        If(LEqual(Arg0, Buffer(0x10)
        {
            0x1e, 0x54, 0x81, 0x76, 0x27, 0x88, 0x39, 0x42, 0x8d, 0x9d, 0x36, 0xbe, 0x7f, 0xe1, 0x25, 0x42
        }))
        {
            If(LEqual(Arg2, Zero))
            {
                Return(Buffer(One)
                {
                    0x03
                })
            }
            If(LEqual(Arg2, One))
            {
                Return(Buffer(0x4)
                {
                    0x00, 0x01, 0x02, 0x03
                })
            }
        }
        Else
        {
            Return(Buffer(One)
            {
                0x00
            })
        }
    } // Method(_DSM ...)
} // Device(SPBA)

