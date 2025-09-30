--------------------------------
-- INSTANTS
--------------------------------
CREATE OR REPLACE TABLE Instants (
    InstantId integer PRIMARY KEY,
    Instant timestamptz NOT NULL );

COPY Instants(InstantId, Instant) FROM './data/instants.csv';

CREATE OR REPLACE VIEW Instants1(InstantId, Instant) AS
    SELECT InstantId, Instant
    FROM Instants
    ORDER BY InstantId
    LIMIT 10;