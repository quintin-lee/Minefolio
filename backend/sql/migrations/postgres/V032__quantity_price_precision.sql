-- V032__quantity_price_precision.sql (PostgreSQL)
--
-- Widen quantity / price / cost-basis columns from DECIMAL(18,4) to NUMERIC(28,8)
-- so the 8-decimal fixed-point values written by the ledger engine and
-- transaction path survive without rounding in the database.
--
-- Note: assets.current_value remains DECIMAL(18,2) (money precision, unchanged);
-- transaction_lots.cost_per_unit remains TEXT (already arbitrary precision).

ALTER TABLE assets ALTER COLUMN quantity   TYPE NUMERIC(28,8);
ALTER TABLE assets ALTER COLUMN cost_basis TYPE NUMERIC(28,8);
ALTER TABLE assets ALTER COLUMN net_value  TYPE NUMERIC(28,8);

ALTER TABLE transactions ALTER COLUMN quantity       TYPE NUMERIC(28,8);
ALTER TABLE transactions ALTER COLUMN price_per_unit TYPE NUMERIC(28,8);
