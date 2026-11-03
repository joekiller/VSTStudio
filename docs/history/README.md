# Where VideoSpeedTracker came from

VideoSpeedTracker began on one street in Charlottesville, Virginia, as one neighbor's answer to a question the whole
block was asking: *how fast are those cars really going?* This page tells that story, credits the people who built the
original, and draws out what it shows about turning a neighborhood worry into something a city can act on. Everything
the project does today rests on that work.

## Paul Reynolds and Locust Avenue

[Paul F. Reynolds](https://www.cs.virginia.edu/~pfr) is a retired University of Virginia computer science professor
who, in his own words, brought "40 years of experience with tracking moving objects" to the street outside his house.
Locust Avenue runs through the Martha Jefferson neighborhood between downtown and the U.S. 250 bypass, and residents
there saw speeding every day.

Late in 2015 he aimed an inexpensive outdoor camera (a Foscam FI9803EP, about $90 then) at Locust Avenue and wrote
software to measure every car that passed. The approach was the one radar engineers use to track aircraft: find
moving things by comparing one video frame with the next, then follow each vehicle's front bumper with a
*predictive tracker* that keeps an estimate of where the bumper should be. When two cars going opposite ways overlap
on screen, the tracker *dead reckons* both bumpers from their predicted paths until the cars separate. A car's speed
is how many frames it takes to cross a fixed zone on the picture, compared with a calibration car driven through the same zone at
a known speed. He published the code on GitHub in February 2016 as
[pfr/VideoSpeedTracker](https://github.com/pfr/VideoSpeedTracker), under the BSD 3-Clause license, so anyone could
check how the numbers were made or use it on their own street.

### Bringing the numbers to the city

On February 1, 2016, he put two months of data in front of the city in a short briefing,
[*Excessive City Vehicle Speeds As Observed from Locust Avenue*](2016-02-locust-avenue-speeds-for-council.pdf). From
about 28,000 vehicles measured in December 2015 and January 2016 it reported that:

- under normal conditions about 85% to 90% of vehicles were over the 25 mph limit;
- speed trailers, on the street for three weeks from December 31, lost nearly all their effect over that time: on their last
  day 78% of vehicles were still speeding, with top speeds back where they had been;
- snow that narrowed the lanes was a natural experiment, and about 64% still sped;
- city vehicles, buses among them, were part of the problem, so the city could also be part of the fix.

It closed with a specific, ready-to-adopt ask: a draft council resolution for a "zero tolerance" policy on speeding
by city-owned and city-operated vehicles, with training, enforcement and regular reports back to council. The
briefing also notes that residents were already working with the city's Neighborhood Development Services department
on street changes.

Local press followed the work through 2016. Charlottesville Tomorrow's coverage, collected on
[cvillepedia's Locust Avenue page](https://www.cvillepedia.org/Locust_Avenue), includes residents being given
traffic-calming choices (January 2016), the citizen-built speed tracker itself (January 2016), citizen-powered speed
cameras finding new danger zones in other neighborhoods (April 2016), and a city engineer briefing Martha Jefferson
residents on Locust Avenue traffic calming (February 2018). The software spread too: by that summer it was in use as
far away as China, with interest from England and Canada.

### Measuring the city's own pilot

The best-known chapter is in
[Charlottesville Tomorrow's August 2016 report](https://www.cvilletomorrow.org/locust-avenue-traffic-update/). In
late July 2016 the city painted a $6,500 pilot on Locust Avenue: narrower 11 ft travel lanes, an edge line, marked
parking, a wider southbound bike lane and shared-lane markings, designed with input from the Martha Jefferson
neighborhood, to test cheap lane narrowing before paying for permanent changes. Because Reynolds had months of
measurements from before the paint, he could say what it did. Compliance with the 25 mph limit, about 45%
southbound and 25% northbound beforehand, rose 4 to 5 points southbound and about 1 point northbound for roughly three
days, then went back to where it had been by the end of the first week. He had also tracked city buses, which kept
to the limit about 95% of the time.

The response is worth reading in full. The director of Neighborhood Development Services was not surprised, saying
speed measures often fail without enforcement and that better transit would also improve safety. The neighborhood
association's president wanted a longer observation period. A cyclist who rode the southern blocks felt safer there.
A city councilor wanted one bike-lane line blacked out because drivers still crossed it. And council was about to take
up its *Streets That Work* design guidelines. One neighbor's camera had become a shared, trusted measurement that
staff, residents and elected officials could all argue from.

## What the story teaches about working with your city

None of this needed special access. It needed patience, an honest method and a constructive tone. The habits that
made it work are the ones this project tries to make easy for everyone:

1. **Measure before you argue.** "Everyone speeds" is an opinion; "85% of 28,000 cars were over the limit" is a
   finding. Count every car, not just the scary ones, and run long enough to cover weekdays, weekends and weather.
2. **Measure what the city tries.** Speed trailers, signs and paint are experiments. A resident with a steady
   before-and-after record can tell staff, at no cost to them, whether an experiment worked, and that makes the
   resident a partner rather than a complainant.
3. **Show your method and its limits.** Open code, a calibration you can describe, and plain statements of what the
   data can't show are what let a traffic engineer take a resident's numbers seriously.
4. **Go through the existing channels.** Neighbors and the neighborhood association first, then the city staff who
   handle traffic and neighborhood streets, then council. Each has a different job, and each needs the evidence in a
   different form.
5. **Bring a specific ask.** The briefing didn't just say "do something"; it handed council a resolution it could
   pass. A concrete request (a speed study, a traffic-calming review, a change to the city's own fleet) is far easier
   to act on.
6. **Respect people's privacy and the staff's expertise.** The point is the street's behavior, not any one driver.
   Staff may know constraints you don't (budgets, enforcement, transit, emergency access), and their view belongs in
   the conversation.
7. **Stay with it.** Pilots take weeks to judge and years to build. Keeping the camera running is what turned a first
   alarming report into a record of what changed and what didn't.

[Working with your city](../working-with-your-city.md) turns these into practical steps with today's tools: the
street report, before-and-after comparisons, verified calibration and published datasets.

## From 2016 to today

Joseph Lawson's fork, [joekiller/VideoSpeedTracker](https://github.com/joekiller/VideoSpeedTracker), began in October 2026
to bring VST to a much narrower view, a second-floor window over a one-way street in Charlottesville where wrong-way
drivers matter as much as speeders, and to make it usable by anyone. The tracker at its heart is still Paul Reynolds'
design, and his copyright and license notice is in every source file. VST Studio recognizes a setup made with the 2016
program, greets its creator (the Locust Avenue setup gets "Hello, Professor."), and builds on it without changing it
([VST Studio](../vst-studio.md#coming-from-the-2016-videospeedtracker)). Around the tracker, the project has changed a
great deal:

| | 2016 original | Today |
|---|---|---|
| Platforms | Windows 7, 64-bit | Windows, macOS, Linux and Raspberry Pi, with signed, verifiable release packages |
| Library | OpenCV 2.4.11, DLLs installed by hand | OpenCV 5 built into every package; nothing extra to install |
| Setup | edit `VST.cfg` by hand, in a fixed order | **VST Studio**, a guided desktop app, plus a phone companion for setting up from the curb |
| Cameras | a network camera recording 1280x720 at exactly 30 fps | any video size and frame rate, USB webcams, phones, network streams, Raspberry Pi cameras, live or recorded |
| Views | about 50 ft of a two-way street | down to about 14 ft of street, angled views, one-way streets with wrong-way callouts |
| Calibration | drive past at the speed limit and count frames | drive past at any speed, cones or a pole, a car of known model, a phone's GPS or a Tesla dashcam, combined and checked with a second drive |
| Results | a csv per video and a highlights video | live results, a printable street report, archives, and published datasets anyone can re-check |

Other people helped the original along in 2016: Patrick Reynolds worked on Mac and Linux builds and fixed config and
loop bugs, SecureBot turned the manual into Markdown and packaged the Windows libraries, and a `neighborhoods` branch
extended VST to several Charlottesville neighborhoods. What happened to every issue, pull request and fork of the original is recorded in
[upstream-issues.md](upstream-issues.md).

## The original materials

These are kept as they were, for anyone who wants to read the original work:

- [The original README](2016-readme.md) (March 2016).
- [Video Speed Tracker Overview and User Manual](2016-user-manual.md) (2016), also as a
  [PDF](2016-user-manual-v3.pdf). Still the clearest explanation of how the tracker works, with Paul Reynolds'
  annotated screenshots.
- [Excessive City Vehicle Speeds As Observed from Locust Avenue](2016-02-locust-avenue-speeds-for-council.pdf), the
  February 2016 briefing.
- [A video of VST on a 25 mph residential street](https://www.youtube.com/watch?v=1HVkKysDvGA).
- The sample video every release still ships, `manual_20160130172251.avi`: about eight and a half minutes of Locust
  Avenue on January 30, 2016, which is what VST Studio opens before you have a camera of your own.
