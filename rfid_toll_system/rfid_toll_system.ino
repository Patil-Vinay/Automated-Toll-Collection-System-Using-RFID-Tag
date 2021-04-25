#include <SPI.h>
#include <MFRC522.h>
#include <OnewireKeypad.h>
#include <LiquidCrystal.h>
#include <Servo.h>

// rs, en, d4, d5, d6, d7
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

Servo servo;

#define servoPin 9
#define gateUp 30     // servo angle with the barrier open
#define gateDown 120  // servo angle with the barrier closed

// IR sensors give LOW when something is in front of them
#define sensorPin1 A2  // before the gate
#define sensorPin2 A3  // at the gate
#define buzzerPin A4

#define RST_PIN 8
#define SS_PIN 10

MFRC522 mfrc522(SS_PIN, RST_PIN);

#define TOLL 500
#define NUM_CARDS 2

// registered tags and their balance in Rs
const char *cardUid[NUM_CARDS] = {"84 E6 E0 1F", "43 88 D9 73"};
long balance[NUM_CARDS] = {5000, 300};

int senVal1 = HIGH;
int senVal2 = HIGH;

int lastCard = -1;  // last card scanned, recharge goes to this one
bool gateOpen = true;
bool paid = false;
bool rechargeMode = false;

#define MAX_DIGITS 6
char amount[MAX_DIGITS + 1];
byte digitCount = 0;

char KEYS[] = {
  '1', '2', '3', 'A',
  '4', '5', '6', 'B',
  '7', '8', '9', 'C',
  '*', '0', '#', 'D'
};

// whole keypad on A0 through the 4.7k / 1k resistor ladder
OnewireKeypad <Print, 16 > KP2(Serial, KEYS, 4, 4, A0, 4700, 1000, ExtremePrec );

void setup()
{
  lcd.begin(16, 2);
  Serial.begin(9600);
  servo.attach(servoPin);
  servo.write(gateUp);

  pinMode(sensorPin1, INPUT);
  pinMode(sensorPin2, INPUT);
  pinMode(buzzerPin, OUTPUT);

  KP2.SetKeypadVoltage(5.0);

  SPI.begin();
  mfrc522.PCD_Init();

  showMessage(" Automatic Toll", "   Collection", 3000);
}

void loop()
{
  if (rechargeMode)
  {
    reCharge();
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("   Welcome!!!");
  sensorRead();
  rfid();
  keyPad();

  // new vehicle, close the barrier and ask for the card
  if (senVal1 == LOW && gateOpen)
  {
    closeGate();
    paid = false;
    showMessage("Vehicle detected", "", 2000);
    showMessage("Put your card to", "the reader......", 4000);
  }
  // paid and reached the gate, open it
  else if (senVal2 == LOW && paid)
  {
    openGate();
    paid = false;
    showMessage("Have a safe", "journey", 3000);
  }
}

void sensorRead()
{
  senVal1 = digitalRead(sensorPin1);
  senVal2 = digitalRead(sensorPin2);
}

void closeGate()
{
  for (int pos = gateUp; pos <= gateDown; pos++)
  {
    servo.write(pos);
    delay(5);
  }
  gateOpen = false;
}

void openGate()
{
  for (int pos = gateDown; pos >= gateUp; pos--)
  {
    servo.write(pos);
    delay(5);
  }
  gateOpen = true;
}

int findCard(String uid)
{
  for (int i = 0; i < NUM_CARDS; i++)
  {
    if (uid == cardUid[i])
      return i;
  }
  return -1;
}

void rfid()
{
  if (!mfrc522.PICC_IsNewCardPresent())
    return;
  if (!mfrc522.PICC_ReadCardSerial())
    return;

  // uid as "84 E6 E0 1F"
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++)
  {
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  content.toUpperCase();
  content = content.substring(1);
  mfrc522.PICC_HaltA();

  // handy for adding new tags
  Serial.println(content);

  int c = findCard(content);
  if (c < 0)
  {
    digitalWrite(buzzerPin, HIGH);
    showMessage("Unknown Vehicle", "Access denied", 1500);
    digitalWrite(buzzerPin, LOW);
    return;
  }
  lastCard = c;
  beep();

  // nobody waiting at the gate or already paid, so only show the balance
  if (gateOpen || paid)
  {
    showBalance("Your balance", c, 2000);
    return;
  }

  if (balance[c] >= TOLL)
  {
    balance[c] -= TOLL;
    showMessage("  Successfully", " paid your bill", 1500);
    showBalance("Balance left", c, 2000);
    paid = true;
  }
  else
  {
    showMessage("  Your balance", "is insufficient", 1500);
    showBalance("Your balance", c, 3000);
    showMessage("Please Recharge", "", 2000);
  }
}

void keyPad()
{
  if (KP2.Key_State() != PRESSED)
    return;

  char key = KP2.Getkey();
  if (key != 'A')
    return;

  if (lastCard < 0)
  {
    showMessage("Scan your card", "first", 1500);
    return;
  }

  showMessage("Recharging Mode.", "................", 1500);
  clearAmount();
  rechargeMode = true;
}

// digits for the amount, D to add it, C to cancel
void reCharge()
{
  lcd.setCursor(0, 0);
  lcd.print("Enter the amount");

  if (KP2.Key_State() != PRESSED)
    return;

  char key = KP2.Getkey();
  if (key == 'D')
  {
    balance[lastCard] += atol(amount);
    showBalance("Current balance", lastCard, 3000);
    rechargeMode = false;
  }
  else if (key == 'C')
  {
    lcd.clear();
    rechargeMode = false;
  }
  else if (key >= '0' && key <= '9' && digitCount < MAX_DIGITS)
  {
    amount[digitCount] = key;
    lcd.setCursor(digitCount, 1);
    lcd.print(key);
    digitCount++;
  }
}

void clearAmount()
{
  memset(amount, 0, sizeof(amount));
  digitCount = 0;
}

void beep()
{
  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(buzzerPin, LOW);
  delay(100);
}

void showMessage(const char *line1, const char *line2, int wait)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  delay(wait);
  lcd.clear();
}

void showBalance(const char *title, int c, int wait)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(title);
  lcd.setCursor(0, 1);
  lcd.print("Rs. ");
  lcd.print(balance[c]);
  delay(wait);
  lcd.clear();
}
