# m5din-solar-relay
Relay that enables when power prices are negative or on the lowest hours of the day.
Uses a m5din meter, please modify the code to include your WiFi credentials. A solid state relay is put on pin 1,
a voltage divider to read the battery voltage is put on pin 2 of the m5din meter. 

If you do not want to use it to charge a home battery in case it goes too low, set MIN_BATT_VOLTAGE to 0.

This software retrieves the power prices from the internet and enables if:
It's in the cheapest 4 hours of the day.
OR power supplier prices are negative
OR the house battery is empty

