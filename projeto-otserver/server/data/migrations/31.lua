-- Database version 32: star evolution for the vocation collection.
--  * adds `player_vocation_collection`.`stars` (every vocation of every character starts with 1 star)
--  * the unlock now CONSUMES the 100 fragments. Vocations that were already unlocked under the old
--    rules (fragments were never consumed) get those 100 fragments deducted once, so everything above
--    100 becomes available for stars. Vocations still locked are untouched (they keep every fragment
--    and pay the 100 when they are unlocked). Nothing is ever lost: fragments never drop below 0.
-- Safe to re-run: the column is only added if missing and the deduction is guarded by a marker that is
-- written in the same transaction as the deduction.
local UNLOCK_COST = 100 -- same value as VocationCollection.FRAGMENTS_REQUIRED when this migration was written
local MARKER = "vocation_stars_fragments_migrated"

local function columnExists(table_name, column)
	local res = db.storeQuery(string.format("SELECT 1 FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = %s AND `COLUMN_NAME` = %s LIMIT 1", db.escapeString(table_name), db.escapeString(column)))
	if res then
		result.free(res)
		return true
	end
	return false
end

function onUpdateDatabase()
	print("> Updating database to version 32 (vocation stars)")

	if not columnExists("player_vocation_collection", "stars") then
		if not db.query("ALTER TABLE `player_vocation_collection` ADD COLUMN `stars` tinyint unsigned NOT NULL DEFAULT '1'") then
			return false
		end
	end

	local done = db.storeQuery(string.format("SELECT 1 FROM `server_config` WHERE `config` = %s LIMIT 1", db.escapeString(MARKER)))
	if done then
		result.free(done)
		return true
	end

	db.query("START TRANSACTION")
	local ok = db.query(string.format("UPDATE `player_vocation_collection` SET `fragments` = `fragments` - %d WHERE `unlocked` = 1 AND `fragments` >= %d", UNLOCK_COST, UNLOCK_COST))
		and db.query(string.format("INSERT INTO `server_config` (`config`, `value`) VALUES (%s, '1')", db.escapeString(MARKER)))
	if not ok then
		db.query("ROLLBACK")
		return false
	end
	db.query("COMMIT")
	return true
end
