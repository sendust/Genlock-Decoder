/*
  Genlock decoder using TI lmh1981, ESP32
  Code managed by sendust (SBS)
  2023/1/16   Genlock monitoring enable (SONY SW, MONITOR LED)
  2023/1/17   SONY SWLED OFF with inconstient v sync
  2023/1/18   EVEN/ODD field information serial out/print.
*/

#define HS_PIN 36
#define OE_PIN 39
#define BP_PIN 34
#define CS_PIN 35
#define VF_PIN 32
#define VS_PIN 33

#define DLED1 25
#define DLED2 26
#define DLED3 27
#define DLED4 14
#define DLED5 13
#define DLED6 2

#define SONYSW 23
#define SONYLED 19

#define LCDSDA 21
#define LCDSCL 22

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>


int count_hsmax;

class inpin
{
  public:
  inpin(int p_no)
  {
    pinMode(p_no, INPUT_PULLUP);
    _p_no = p_no;
    _last = 1;
    _count = 0;
    _time_now = 0;
  }

  int scan()
  {
    _now = digitalRead(_p_no);
    int result = _now - _last;
    _last = _now;
    return result;
  }

  int value()
  {
    return digitalRead(_p_no);
  }

  unsigned long _time_now;
  int _count;

  private:
    int _p_no;
    int _now, _last;

    

};

class outpin
{
  public:
  outpin(int p_no)
  {
    pinMode(p_no, OUTPUT);
    _p_no = p_no;
  }

  void value(int value)
  {
    digitalWrite(_p_no, value);
    _state = value;
  }

  void toggle()
  {
    _state = !_state;
    digitalWrite(_p_no, _state);
  }

  private:
    int _p_no;
    int _state;

};


class heartbeat : public outpin
{
  public:
    heartbeat(int p_no, int duty, int period) 
    : outpin(p_no)
    {
      _duty = duty;
      _duty_count = int(period * duty / 100);
      _period = period;
      _count = 0;
    }

    void run()
    {
      _count++;
      if ((_count == 1) | (_count == _duty_count)) toggle();
      else if (_count == _period) _count = 0;      
    }


  private:
    int _duty, _duty_count, _period;
    unsigned long _count;
};


class average
{
	public:
		average(int N)
		{
			nbr = N;
			list = new (int)(N);
			int * ptr = list;
			for (int i = 0; i < N; i++)
			{
				*ptr = 0;
				ptr++;
			}
		}
		
		
		float get_average()
		{
			int sum = 0;
			int * ptr = list;
			for (int i = 0; i < nbr; i++)
				{
					sum += *ptr;
					ptr++;
				}
			return float(sum) / nbr;
		}
		
		void push(int value)
		{
			int * ptr = list;
			for (int i = 0; i < nbr - 1; i++)
			{
				*ptr = *(ptr + 1);
				ptr++;
			}
			*ptr = value;
		}

	private:
		int* list;
		int nbr;
};

class scantype    
{
  public:
    const char type[2][17] = {"progressive     ", "interlaced      "};
    int score_int;
    int _sum_hs[2];

    scantype()
    {
      score_int = 0;
      _sum_hs[0] = 0;
      _sum_hs[1] = 0;
    }

    int get_sum_hs()
    {
      return (_sum_hs[0] + _sum_hs[1]);
    }

    void reset_sum_hs()
    {
      _sum_hs[0] = 0;
      _sum_hs[1] = 0;
    }

};


class FLAGS
{
  public:
    FLAGS()
    {
      _printfield = 0;
      _printerror = 0;
      _field[0] = "ev!";
      _field[1] = "od!";
    }
    
    bool _printfield;
    bool _printerror;
    char **_field = new char *[4];
    char *_f;
};


FLAGS flag;
average avg(20);

inpin HS(HS_PIN);
inpin OE(OE_PIN);
inpin BP(BP_PIN);
inpin CS(CS_PIN);
inpin VF(VF_PIN);
inpin VS(VS_PIN);
inpin sonysw(SONYSW);


outpin LED1(DLED1);
outpin LED2(DLED2);
outpin LED3(DLED3);
outpin LED4(DLED4);
outpin LED5(DLED5);
heartbeat LED6(DLED6, 10, 40000);
outpin swled(SONYLED);
LiquidCrystal_I2C lcd(0x27, 16, 2);       //0x27 or 0x3F

scantype st;

void isr_vs()
{
  unsigned long now = micros();
  if (now > VS._time_now)           // avoid overflow
    avg.push(now - VS._time_now);   // push vs period
  VS._time_now = now;
  int oe = OE.value();     // read odd/even value
  st.score_int += !oe;     // detect odd/even pulse
  LED1.toggle();
  LED3.value(oe);
  
  flag._f = flag._field[oe];    // Get odd even information
  flag._printfield = 1;         // Ready to print
  
  st._sum_hs[oe] = HS._count;
  count_hsmax = st.get_sum_hs();    // add even, odd field sync sum
  HS._count = 0;
}

void isr_hs()
{
  HS._count++;
}



void setup() {
  // put your setup code here, to run once:
  LED1.value(0);
  LED2.value(0);
  LED3.value(0);
  LED4.value(0);
  LED5.value(0);
  LED6.value(0);
  swled.value(0);

  Wire.setClock(800000);

  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Start Genlock Decoder....  CPU clock is ");
  Serial.println(getCpuFrequencyMhz());

  Serial2.begin(115200);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Genlock Decoder ");
  lcd.setCursor(0,1);
  lcd.print("Made by sendust ");
  
  Serial.println("I2c clock rate is");
  Serial.println(Wire.getClock());

  delay(500);
  attachInterrupt(VS_PIN, isr_vs, RISING);
  attachInterrupt(HS_PIN, isr_hs, FALLING);
  
  count_hsmax = 0;
}

unsigned long last_print = 0;
int count_vs_last = 0;


void loop() {
  // put your main code here, to run repeatedly:
  unsigned long now = millis();
  LED6.run();

  if (sonysw.scan() == 1)     // Engage genlock monitor
    {
      st.reset_sum_hs();
      swled.value(1);
      LED5.value(0);
    }

  if (flag._printfield)
  {
    Serial2.print(flag._f);     // Report even,odd field information
    flag._printfield = 0;
  }
    
  if ((now - last_print) > 1000)
  {
    LED2.toggle();    
    if (VS._count)     //  v sync is found !!
    {
      char buffer[30];
      char freq[12];
      float hz = (float)1/avg.get_average() * 1000000;
      Serial.println(count_hsmax);
      Serial.print(hz);
      Serial.print(" ");
      Serial.println(st.type[st.score_int && 1]);
      
      lcd.setCursor(0,0);
      lcd.print(st.type[st.score_int && 1]);    // print progressive or interlaced in LCD

      dtostrf(hz, 5, 2, freq);    // convert float to string
      sprintf(buffer, "%sHz %dLine   ", freq, count_hsmax);   // create lcd friendly string
      lcd.setCursor(0,1);
      lcd.print(buffer);



    }
    else    // There is no v sync
    {
      Serial.println("No input...");
      lcd.setCursor(0,0);
      lcd.print("Genlock Decoder ");
      lcd.setCursor(0,1);
      lcd.print("No input        ");
      LED5.value(1);      // Error led on
      swled.value(0);     // SWLED OFF
    }


    if (abs(VS._count - count_vs_last) >= 3)
    {
      Serial.println("V Sync inconsistency detected....");
      LED5.value(1);      // Error led on
      swled.value(0);     // SWLED OFF
      st.reset_sum_hs();    // init hs count
    }
    
    last_print = now;
    count_vs_last = VS._count;
    VS._count = 0;         // Reset vertical sync count
    st.score_int = 0;     // Reset interlace check score..
  }

  if (VS.scan() == 1)     // detect vertical sync without interrupt
  {
    VS._count++;
  }

}
