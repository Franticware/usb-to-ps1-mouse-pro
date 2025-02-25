![PCB](media/pcb.jpg)

## BOM

|Reference|Value|Datasheet|Footprint|Qty||
|-|-|-|-|-|-|
|C1, C2|10u/16V|~|Capacitor_SMD:C_1206_3216Metric|2||
|D1|SS14|https://www.vishay.com/docs/88746/ss12.pdf|Diode_SMD:D_SMA|1||
|J1|Conn_01x09_Pin|~|PS1:Controller_THT90|1||
|J2|USB_A| ~|usb2ps1mouse-specific:USB_A_Sunken|1|![USB A](media/USB_A.jpg)|
|L1|22u|~|usb2ps1mouse-specific:L_combo|1|![Inductor](media/inductor.jpg)|
|R1, R2|22R|~|Resistor_SMD:R_0603_1608Metric|2||
|U1|RP2040-Zero||RP2040_Board:RP2040-Zero|1||
|U2|ME2108A50PG||Package_TO_SOT_SMD:SOT-89-3|1||
||M3x10 countersunk screw + M3 nut|||1+1|![Bolt + nut](media/M3x10.jpg)|

## Manufacturing

I have had the boards made at https://jlcpcb.com using the following parameters:

- PCB Thickness: 1.6
- PCB Color: Black
- Silkscreen: White
- Material Type: FR4
- Via Covering: Tented
- Surface Finish: LeadFree HASL
- Outer Copper Weight: 1 oz
- Board Outline Tolerance: ±0.2mm (Regular)
