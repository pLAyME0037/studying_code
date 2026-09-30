#ifndef CORE_CRUD_MODULES_H_
#define CORE_CRUD_MODULES_H_

#include "crud.h"

// Simple single-table CRUD modules. Compound modules (invoices, imports with
// details) are implemented separately on top of these.

extern Crud_Module crud_module_ill_types;
extern Crud_Module crud_module_ill_prices;
extern Crud_Module crud_module_departments;
extern Crud_Module crud_module_doctors;
extern Crud_Module crud_module_doctor_work_times;
extern Crud_Module crud_module_suppliers;
extern Crud_Module crud_module_medicine_types;
extern Crud_Module crud_module_medicines;
extern Crud_Module crud_module_patients;
extern Crud_Module crud_module_patient_daily_invoices;
extern Crud_Module crud_module_appointments;
extern Crud_Module crud_module_room_types;
extern Crud_Module crud_module_rooms;
extern Crud_Module crud_module_organizations;
extern Crud_Module crud_module_org_invoices;
extern Crud_Module crud_module_org_invoice_details;
extern Crud_Module crud_module_org_payments;

// All simple modules in the order they appear in the dashboard.
extern Crud_Module *crud_modules[];
extern const size_t crud_modules_count;

#endif // CORE_CRUD_MODULES_H_
