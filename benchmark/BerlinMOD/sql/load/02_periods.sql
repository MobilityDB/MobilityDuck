--------------------------------
-- PERIODS
--------------------------------
CREATE OR REPLACE TABLE Periods (
    PeriodId integer PRIMARY KEY,
    Tstart TimestampTz NOT NULL,
    Tend TimestampTz NOT NULL,
    Period tstzspan );

COPY Periods(PeriodId, Tstart, Tend) FROM './data/periods.csv';
UPDATE Periods
SET Period = span(Tstart, Tend);

CREATE OR REPLACE VIEW Periods1(PeriodId, Tstart, Tend, Period) AS
SELECT PeriodId, Tstart, Tend, Period
FROM Periods
ORDER BY PeriodId
LIMIT 10;