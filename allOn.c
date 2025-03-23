#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

// Globale Variablen für Uhrzeit
volatile uint8_t seconds = 0;
volatile uint8_t minutes = 0;
volatile uint8_t hours = 0;
volatile double error = 0.0;

// Helligkeitsstufen für PWM
int currentBrightnessLevel = 1;
const uint8_t brightness_levels[] = {255, 254, 248, 156, 0};

int deepSleep = 0;

// PWM initialisieren
void PWM_Init(void) {
    TCCR1A = (1 << WGM10) | (1 << COM1A1) | (1 << COM1B1); // 8-bit Fast PWM, clear on compare match
    TCCR1B = (1 << WGM12) | (1 << CS11); // Prescaler = 8

    OCR1A = 248; // PWM-Wert für Minuten (PB1)
    OCR1B = 248; // PWM-Wert für Stunden (PB2)
}

// Timer2 für die Uhrzeit initialisieren
void setupTimer2(void) {
    ASSR |= (1 << AS2); // Asynchronmodus aktivieren (32.768 kHz Quarz)

    // Prescaler auf 128 setzen (32.768 kHz / 128 = 256 Hz)
    TCCR2B = (1 << CS22) | (1 << CS20);
    TIMSK2 = (1 << TOIE2); // Timer Overflow Interrupt aktivieren

    // Warten, bis der externe Takt stabil ist
    while (ASSR & ((1 << TCR2BUB) | (1 << TCR2AUB) | (1 << OCR2AUB) | (1 << TCN2UB))) {
        // Warten, bis alle Busy-Flags gelöscht sind
    }
}

// Nicht benötigte Peripherie deaktivieren
void disablePeripherie(void) {
    PRR |= (1 << PRADC);    // ADC deaktivieren
    PRR |= (1 << PRUSART0); // UART deaktivieren
    PRR |= (1 << PRTWI);    // I2C deaktivieren
}

// Schlafmodus aktivieren
void enterSleepMode(void) {
    if (deepSleep == 0) {
        set_sleep_mode(SLEEP_MODE_IDLE);
    } else {
        set_sleep_mode(SLEEP_MODE_PWR_SAVE);
    }

    cli(); // Interrupts deaktivieren
    sleep_enable();
    sei(); // Interrupts aktivieren (zum Aufwecken)
    sleep_cpu(); // MCU schläft jetzt
    sleep_disable(); // Nach dem Aufwachen deaktivieren
}

// Ports initialisieren
void setupPorts(void) {
    // PWM-Ausgänge für Minuten (PB1) und Stunden (PB2) setzen
    DDRB |= (1 << PB1) | (1 << PB2);

    // Port C (PC0 - PC5) und Port D (PD3 - PD7) als Ausgänge setzen
    DDRC = 0x3F;
    DDRD = 0xF8;

    // Alle LEDs ausschalten
    PORTC = 0x00;
    PORTD = 0x00;
}

// Interrupts für Buttons initialisieren
void INT_Init(void) {
    // PD2 als Eingang mit Pull-Up für INT0 (Externer Interrupt)
    DDRD &= ~(1 << PD2);
    PORTD |= (1 << PD2);
    
    // Interrupt an fallender Flanke auslösen
    EICRA |= (1 << ISC01);
    EICRA &= ~(1 << ISC00);
    EIMSK |= (1 << INT0); // Interrupt aktivieren

    // PD1 als normaler Button mit Pull-Up (kein Interrupt)
    DDRD &= ~(1 << PD1);
    PORTD |= (1 << PD1);

    // PB0 als Pin-Change-Interrupt Eingang mit Pull-Up
    DDRB &= ~(1 << PB0);
    PORTB |= (1 << PB0);

    // Pin-Change-Interrupt für PB0 aktivieren
    PCICR |= (1 << PCIE0);
    PCMSK0 |= (1 << PCINT0);
}

// Uhrzeit auf den Binärausgängen anzeigen
void displayTime(void) {
    // Minuten auf PC0-PC5 (6 Bits)
    PORTC = minutes & 0x3F;

    // Stunden auf PD3-PD7 (5 Bits)
    PORTD = (PORTD & 0x07) | ((hours & 0x1F) << 3);
}

// Korrigert für nicht optimale Temperatur des Uhrenquartz
void check_error(void){
    error = error + 0.01875;  // Fehlende Sekunden pro gezählte Sekunde
    if (error >= 1.0) { // Wenn sich eine Schaltsekunde Summiert hat:
        seconds++;
        error = error - 1.0;
    }
}

// Uhrzeit aktualisieren
void updateClock(void) {
    seconds++;
    check_error();
    if (seconds >= 60) {
        seconds = seconds - 60;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) {
                hours = 0;
            }
        }
    }
    displayTime();
}

// Interrupt: Timer-Überlauf (jede Sekunde)
ISR(TIMER2_OVF_vect) {
    updateClock();

    // Prüfen, ob der Button gedrückt wurde (Helligkeitsänderung)
    if (!(PIND & (1 << PD1))) {
        currentBrightnessLevel = (currentBrightnessLevel + 1) % 5;
        OCR1A = brightness_levels[currentBrightnessLevel];
        OCR1B = brightness_levels[currentBrightnessLevel];
    }

    // Wenn die Helligkeit auf 0 ist -> Tiefschlaf aktivieren
    if (currentBrightnessLevel == 0) {
        PORTC = 0 & 0x3F;
        PORTD = (PORTD & 0x07) | ((0 & 0x1F) << 3);
        deepSleep = 1;
    } else {
        deepSleep = 0;
    }
}

// Interrupt: Externer Interrupt für Minuten-Button
ISR(INT0_vect) {
    _delay_ms(5); // Debounce

    if (!(PIND & (1 << PD2))) {
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) {
                hours = 0;
            }
        }
        displayTime();
    }
}

// Interrupt: Pin-Change-Interrupt für Stunden-Button
ISR(PCINT0_vect) {
    _delay_ms(5); // Debounce

    if (!(PINB & (1 << PB0))) {
        if (hours == 23) {
            hours = 0;
        } else {
            hours++;
        }
        displayTime();
    }
}

// Hauptprogramm
int main(void) {
    setupPorts();
    INT_Init();
    PWM_Init();  // PWM für Helligkeitsregelung initialisieren
    setupTimer2();
    sei();       // Globale Interrupts aktivieren

    disablePeripherie();

    while (1) {
        enterSleepMode();
    }

    return 0;  // Wird nie erreicht
}
