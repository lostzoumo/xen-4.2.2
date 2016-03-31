/*
 * ssdt_s3.asl
 *
 * Copyright (c) 2011  Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

DefinitionBlock ("SSDT_S3.aml", "SSDT", 2, "Xen", "HVM", 0)
{
    /*
     * Turn off support for s3 sleep state to deal with SVVP tests.
     * This is what MSFT does on HyperV.
     */
}

