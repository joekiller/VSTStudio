# Working with your city

A camera in your window can tell you how fast your street really is. Getting something done about it means bringing
that evidence to the people who can change the street, in a form they can trust and use. This guide is how to do
that with VideoSpeedTracker. It grew out of the
[Locust Avenue story](history/README.md#paul-reynolds-and-locust-avenue), where one neighbor's measurements became
something residents, city staff and council could all work from.

## 1. Start with your neighbors

- Talk to the people on your block before you mount anything. You'll learn when the worst speeding happens, and a
  shared concern carries more weight than one household's.
- If you have a neighborhood association, bring it in early. Associations often already have a contact in city
  government and know what has been tried before.
- Be clear about what the camera records: each vehicle's speed, direction and time, with a snapshot and short clip
  kept on your computer. VST doesn't read plates or recognize faces (see [Privacy](#privacy-and-fairness) below).

## 2. Collect data people can trust

- **Calibrate, then check it.** In VST Studio's Calibrate step, calibrate however suits you, then **Check it with a
  second drive**. A direction marked **Verified** is the first thing a traffic engineer will ask about, and the
  calibration history records every pass so you can show your work.
- **Run long enough.** Two weeks covers weekdays, weekends and some weather. VST Studio keeps going unattended,
  reconnects a camera that drops out, warns if the camera moves, and logs each day, so gaps are recorded rather than
  hidden.
- **Measure before a change, and after.** If the city has a speed trailer coming, a sign going up or a paint pilot
  planned, keep the camera running through it. The street report's **Compare with another period** puts before and
  after side by side. On Locust Avenue, that is exactly what showed a $6,500 paint pilot helped for about three days.
- **Leave your own drives out.** Passes you use on the Calibrate step, or mark with **That was me** on the phone, are
  tagged and left out of the counts and the report.

## 3. Bring the numbers in the form people use

| Who | What to bring |
|---|---|
| Neighbors | The phone companion's report card, or the first page of the street report. One sentence: "Most cars on our block go faster than the limit, and here's when it's worst." |
| City traffic or transportation staff | The full street report (**Make report** in Results). It gives the 85th-percentile speed (the figure engineers use to review a limit), the share over the limit and 10 or more over, volumes per hour, when speeding happens, the error band, and how the calibration was checked. |
| Anyone who wants to check your work | A published dataset (**Publish** on a calibration set). It is frozen with checksums, opens in any browser, says in one line how accurate it is, and records every later re-check ([dataset-schema.md](dataset-schema.md)). |
| Council | Two or three numbers, one picture, and one specific request. The street report's cover page is meant to be read by someone with no background in traffic data. |

Every report is labelled *Community traffic study. Estimated speeds, ±X mph*, with X worked out from your setup.
That is honest, and it is what makes the numbers credible: you are offering evidence for the city to act on, not
issuing tickets.

## 4. Ask for something specific

"Do something about speeding" is hard to act on. These are easier:

- a speed and volume study by the city's own staff, to confirm your numbers;
- a traffic-calming review for your block, or a place on the list for one;
- enforcement during the hours your report shows are worst;
- a change to how the city's own vehicles drive, as the Locust Avenue briefing proposed for city buses and trucks;
- a pilot (paint, signs, planters) that you will measure before and after, at no cost to the city.

The last one makes you a partner: staff get a free evaluation of something they wanted to try anyway.

## 5. Listen, and stay with it

City staff know things you don't: budgets, emergency access, transit routes, what has worked on similar streets, and
which measures need enforcement to stick. Ask what evidence would help them, and bring that. Expect it to take time.
Pilots need weeks to judge and permanent changes take seasons. Keep the camera running, archive each period with a
name ("Before the new signs"), and come back with the after.

## Privacy and fairness

- VST measures the street, not people. It doesn't read plates, recognize faces, or keep lists of repeat drivers, and
  the street report names a block, never a house number.
- Example frames in a street report are off by default; when included, they are shrunk and the plate area is
  blurred. Archive viewers and clips are **not** blurred, so share those only with people you'd show the street to.
- Never post a clip to shame a driver. The point is to change the street's design or enforcement, which helps
  everyone, including the drivers.
- VST's speeds are estimates from a community study. They are not radar or lidar readings, and nobody should be
  ticketed or accused on the strength of them.
