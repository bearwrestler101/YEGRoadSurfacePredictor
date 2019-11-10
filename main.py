from lxml import html
from selenium import webdriver
import re
import requests
import time
import datetime

def calc_rate(temperature):
    return (0.0029*temperature**2 + 0.059*temperature + 1.5088)/6
    
def calc_freeze_rate(temperature):
    return (-0.3*temperature + 0.0497)/6
    
def fetch_temp():
    hourly_url = 'https://weather.gc.ca/past_conditions/index_e.html?station=xec'

    page = requests.get(hourly_url)
    tree = html.fromstring(page.content)

    temperature = tree.xpath('//*[@id="past24Table"]/tbody/tr[2]/td[3]/text()')
    temperature[0] = re.search('\(([^)]+)', temperature[0]).group(1)
    return float(temperature[0])
    
def fetch_correction_snow():
    date = datetime.date.today()
    date = str(date)
    date = date.split("-")
    month = int(date[1])
    day = int(date[2])-1
    if (day-1) == 1:
        month -= 1
    snow_url = ('https://climate.weather.gc.ca/climate_data/daily_data_e.html?StationID=27214&month=%d&day=%d&timeframe=4&StartYear=1840&EndYear=2019&Day=8&Year=2019&Month=11#' % (month,day))
    page = requests.get(snow_url)
    tree = html.fromstring(page.content)
    if tree.xpath('//*[@id="dynamicDataTable"]/table/tbody/tr[%d]/th/text()' % (day+1)) == "Sum":
        print("No new data")
        return
    else:
        return tree.xpath('//*[@id="dynamicDataTable"]/table/tbody/tr[%d]/td[9]/text()' % day)[0]
        
def fetch_precip(driver=None):
    owning = False
    if (not driver):
        owning = True
        driver = webdriver.Chrome(executable_path = "C:/Users/makha/Desktop/HackED2019/chromedriver.exe")
        time.sleep(.5)
    try:
        driver.get("https://www.theweathernetwork.com/ca/weather/alberta/edmonton")
    except:
        pass
    e = driver.find_element_by_class_name("description.unit_c")
    text = e.get_attribute("innerText")
    precip_type = "snow" if len(re.findall("snow", text.lower())) else "rain"
    try:
        precip_amount = int(re.findall("\d+ cm", text)[0][:-3])
    except:
        precip_amount = 0
    temp = driver.find_element_by_class_name("temp").get_attribute("innerText")
    if (owning):
        driver.close()
    return float(temp), (precip_type, precip_amount)

class RoadState:
    def __init__(self, snow, water, ice):
        self.snow = snow
        self.water = water
        self.ice = ice
    def __str__(self):
        return "Snow:%.2f, Water:%.2f, Ice:%.2f" % (self.snow, self.water, self.ice)
    def safety(self):
        self.snow = max(self.snow, 0)
        self.water = max(self.water, 0)
        self.ice = max(self.ice, 0)
        
def calc_melt(road_state, temperature):
    if temperature > 0.0:
        road_state.water += calc_rate(temperature) * road_state.ice * 0.91
        road_state.water += calc_rate(temperature) * road_state.snow * 0.10
        road_state.water *= 0.96
        road_state.ice -= road_state.ice * calc_rate(temperature)
        road_state.snow -= road_state.snow * calc_rate(temperature)
        road_safety.safety()

def calc_freeze(road_state, temperature):
    if temperature <= 0.0:
        road_state.ice += calc_freeze_rate(temperature) * road_state.water * 1.10
        road_state.water -= calc_freeze_rate(temperature) * road_state.water    
        road_state.safety()
            
def calc_tire_affect(road_state, temperature):
    if temperature < 0.0:
        road_state.ice -= calc_rate(temperature) * road_state.ice * 0.11 * 0.1
        road_state.water -= calc_rate(temperature) * road_state.water * 0.1 * 0.1 
        road_state.snow -= calc_rate(temperature) * road_state.snow *0.1
        road_state.safety()
            
def tick(road_state, history, temp= None, precip = None):
    # fetch data
    # update based on data
    corrected_snow = fetch_correction_snow()
    if corrected_snow == None:
        update_road_state(road_state, history, temp, precip)
    else:
        recent_temps = history.fetch_24temp()
        if None in recent_temps:
            update_road_state(road_state, history, temp, precip)
            return road_state
        road_state = (corrected_snow, *recent_temps[0][0][1:])
        for i in range(24):
            update_road_state(road_state, history, *recent_temps[i][1:])
    return road_state
    
def update_road_state(road_state, history, temp= None, precip = None):
    if (temp == None or precip == None):
        temp, precip = fetch_precip()
    history.addition(road_state,temp,precip)
    #temp = fetch_temp()
    if precip[0] == "snow":
        road_state.snow += precip[1]/6
    elif precip[1] == "rain":
        road_state.water += precip[1]/6
    
    calc_melt(road_state, temp)
    calc_freeze(road_state, temp)
    calc_tire_affect(road_state, temp)
    
class History:
    def __init__(self):
        self.history = [None] * 48
        self.index = 0
    def addition(self, road_state, temperature, precip):
        self.history[self.index] = (road_state, temperature, precip)
        self.index += 1
        self.index %= 48
    def fetch_24temp(self):
        return [self.history[i%48] for i in range(self.index-24,self.index+1)]
        

def main ():
    #print(fetch_correction_snow())
    road_state = RoadState(0.0, 2.0, 1.5)
    history = History()
    while True: 
        road_state = tick(road_state, history)
        
        print(history.__repr__())
        print(road_state)
        
        
        strings = ["Good to drive!", "Lots of snow fell recently, be careful!", "Ice has formed, stay frosty!", "Slush is on the road, keep an eye out", "Roads are wet, keep your withs about you!"]
        ret = ""
        if road_state.ice > 0.2:
            ret = strings[2]
        elif road_state.snow > road_state.ice and road_state.ice < 0.3:
            ret = strings[1]
        elif road_state.water > road_state.snow and road_state.ice < 0.3:
            ret = strings[4]
        elif road_state.water > 0.3 and road_state.ice > 0.1:
            ret = strings[3]
        else:
            ret = strings[0]
        
        final_output = ret + "<br>" + str(road_state) + "</br>"
        print(final_output)
        with open("frontend.html", "r") as f:
            html_file = f.read()
         
        html_file = re.sub('<span>[\s\S]+</span>', "<span>%s</span>" % final_output, html_file) 
        print(html_file)
        with open("frontend.html", "w") as f:
            f.write(html_file)

        
        time.sleep(3600)
        
if __name__== "__main__":
    main()
    
