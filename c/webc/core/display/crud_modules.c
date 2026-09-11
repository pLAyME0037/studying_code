#include "crud_modules.h"

static Crud_Column ill_type_columns[] = {
    { .name = "type",        .label = "Type",        .type = COL_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true  },
};

static Crud_Column ill_price_columns[] = {
    {
        .name = "ill_type_id",
        .label = "Illness Category",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "IllTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "name",        .label = "Name",        .type = COL_TEXT,     .nullable = false },
    { .name = "price",       .label = "Price",       .type = COL_NUM,      .nullable = false },
    { .name = "currency",    .label = "Currency",    .type = COL_TEXT,     .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true },
};

static Crud_Column department_columns[] = {
    { .name = "name",        .label = "Name",        .type = COL_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true  },
};

static Crud_Column doctor_columns[] = {
    { .name = "name",  .label = "Name",    .type = COL_TEXT, .nullable = false },
    { .name = "sex",   .label = "Sex",     .type = COL_TEXT, .nullable = true  },
    { .name = "skill", .label = "Skill",   .type = COL_TEXT, .nullable = true  },
    { .name = "tel",   .label = "Tel",     .type = COL_TEXT, .nullable = true  },
    { .name = "addr",  .label = "Address", .type = COL_TEXT, .nullable = true },
};

static Crud_Column doctor_work_time_columns[] = {
    {
        .name = "doctor_id",
        .label = "Doctor",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Doctors",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "department_id",
        .label = "Department",
        .type = COL_FK_SELECT,
        .nullable = true,
        .fk_table = "Departments",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "time", .label = "Time", .type = COL_TEXT, .nullable = false },
};

static Crud_Column supplier_columns[] = {
    { .name = "company",     .label = "Company",     .type = COL_TEXT,     .nullable = false },
    { .name = "contact",     .label = "Contact",     .type = COL_TEXT,     .nullable = true  },
    { .name = "addr",        .label = "Address",     .type = COL_TEXT,     .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true  },
};

static Crud_Column medicine_type_columns[] = {
    { .name = "type",        .label = "Type",        .type = COL_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true  },
};

static Crud_Column medicine_columns[] = {
    {
        .name = "medicine_type_id",
        .label = "Medicine Category",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "MedicineTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    {
        .name = "supplier_id",
        .label = "Supplier",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Supplier",
        .fk_label = "company",
        .fk_value = "id"
    },
    { .name = "name",        .label = "Name",        .type = COL_TEXT,     .nullable = false },
    { .name = "price",       .label = "Price",       .type = COL_NUM,      .nullable = false },
    { .name = "currency",    .label = "Currency",    .type = COL_TEXT,     .nullable = true  },
    { .name = "percent",     .label = "Percent",     .type = COL_NUM,      .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true },
};

static Crud_Column patient_columns[] = {
    { .name = "name",          .label = "Name",       .type = COL_TEXT, .nullable = false },
    { .name = "sex",           .label = "Sex",        .type = COL_TEXT, .nullable = true  },
    { .name = "age",           .label = "Age",        .type = COL_NUM,  .nullable = true  },
    { .name = "tel",           .label = "Tel",        .type = COL_TEXT, .nullable = true  },
    { .name = "patient_type",  .label = "Patient Type", .type = COL_TEXT, .nullable = true },
    { .name = "register_date", .label = "Register Date", .type = COL_DATE, .nullable = true },
    { .name = "district",      .label = "District",   .type = COL_TEXT, .nullable = true  },
    { .name = "province",      .label = "Province",   .type = COL_TEXT, .nullable = true  },
};

static Crud_Column appointment_columns[] = {
    {
        .name = "doctor_id",
        .label = "Doctor",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Doctors",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "date", .label = "Date", .type = COL_DATE, .nullable = true },
    { .name = "time", .label = "Time", .type = COL_TEXT, .nullable = true },
};

static Crud_Column patient_daily_invoice_columns[] = {
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Invoice Date",  .type = COL_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_NUM,  .nullable = true },
};

static Crud_Column patient_daily_invoice_detail_columns[] = {
    {
        .name = "patient_daily_invoice_id",
        .label = "Daily Invoice",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "PatientDailyInvoices",
        .fk_label = "id",
        .fk_value = "id"
    },
    {
        .name = "ill_type_id",
        .label = "Illness",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "IllTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "patientDaily_invoice_detail_price",    .label = "Price",    .type = COL_NUM,  .nullable = true },
    { .name = "patientDaily_invoice_detail_currency", .label = "Currency", .type = COL_TEXT, .nullable = true },
};

static Crud_Column patient_medicine_invoice_columns[] = {
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Invoice Date",  .type = COL_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_NUM,  .nullable = true },
};

static Crud_Column patient_medicine_invoice_detail_columns[] = {
    {
        .name = "patient_medicine_invoice_id",
        .label = "Medicine Invoice",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "PatientDailyInvoices",
        .fk_label = "id",
        .fk_value = "id"
    },
    {
        .name = "medicine_id",
        .label = "Medicine",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "qty",      .label = "Qty",      .type = COL_NUM,  .nullable = true },
    { .name = "price",    .label = "Price",    .type = COL_NUM,  .nullable = true },
    { .name = "amount",   .label = "Amount",   .type = COL_NUM,  .nullable = true },
    { .name = "currency", .label = "Currency", .type = COL_TEXT, .nullable = true },
};

static Crud_Column patient_invoice_out_detail_columns[] = {
    {
        .name = "patient_invoice_out_id",
        .label = "Room Invoice",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "PatientInvoiceOut",
        .fk_label = "id",
        .fk_value = "id"
    },
    {
        .name = "patient_daily_invoice_id",
        .label = "Daily Invoice",
        .type = COL_FK_SELECT,
        .nullable = true,
        .fk_table = "PatientDailyInvoices",
        .fk_label = "id",
        .fk_value = "id"
    },
};

static Crud_Column org_daily_invoice_detail_columns[] = {
    {
        .name = "organization_daily_id",
        .label = "Daily Invoice",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "PatientDailyInvoices",
        .fk_label = "id",
        .fk_value = "id"
    },
    {
        .name = "ill_id",
        .label = "Illness",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "IllTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "price",    .label = "Price",    .type = COL_NUM,  .nullable = true },
    { .name = "currency", .label = "Currency", .type = COL_TEXT, .nullable = true },
};

static Crud_Column org_invoice_out_detail_columns[] = {
    {
        .name = "organization_invoice_out_id",
        .label = "Invoice Out",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "OrganizationInvoiceOut",
        .fk_label = "id",
        .fk_value = "id"
    },
    {
        .name = "organization_invoice_id",
        .label = "Org Invoice",
        .type = COL_FK_SELECT,
        .nullable = true,
        .fk_table = "OrganizationInvoices",
        .fk_label = "invoice_date",
        .fk_value = "id"
    },
};

static Crud_Column org_balance_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "balance_amount", .label = "Balance Amount", .type = COL_NUM, .nullable = true },
    { .name = "balance",        .label = "Balance",        .type = COL_NUM, .nullable = true },
};

static Crud_Column org_invoice_detail_columns[] = {
    {
        .name = "organization_invoice_id",
        .label = "Org Invoice",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "OrganizationInvoices",
        .fk_label = "invoice_date",
        .fk_value = "id"
    },
    {
        .name = "medicine_id",
        .label = "Medicine",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "qty",      .label = "Qty",      .type = COL_NUM,  .nullable = true },
    { .name = "price",    .label = "Price",    .type = COL_NUM,  .nullable = true },
    { .name = "amount",   .label = "Amount",   .type = COL_NUM,  .nullable = true },
    { .name = "currency", .label = "Currency", .type = COL_TEXT, .nullable = true },
};

static Crud_Column room_type_columns[] = {
    { .name = "type",        .label = "Type",        .type = COL_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true  },
};

static Crud_Column room_columns[] = {
    {
        .name = "room_type_id",
        .label = "Room Types",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "RoomTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "name",     .label = "Name",     .type = COL_TEXT, .nullable = false },
    { .name = "price",    .label = "Price",    .type = COL_NUM,  .nullable = false },
    { .name = "currency", .label = "Currency", .type = COL_TEXT, .nullable = true  },
};

static Crud_Column organization_columns[] = {
    { .name = "name",        .label = "Name",        .type = COL_TEXT,     .nullable = false },
    { .name = "start_date",  .label = "Start Date",  .type = COL_DATE,     .nullable = true  },
    { .name = "end_date",    .label = "End Date",    .type = COL_DATE,     .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TEXTAREA, .nullable = true },
};

static Crud_Column org_invoice_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Invoice Date",  .type = COL_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_NUM,  .nullable = true },
};

static Crud_Column org_payment_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "organization_invoice_id",
        .label = "Org Invoice",
        .type = COL_FK_SELECT,
        .nullable = true,
        .fk_table = "OrganizationInvoices",
        .fk_label = "invoice_date",
        .fk_value = "id"
    },
    { .name = "date",    .label = "Date",    .type = COL_DATE, .nullable = true },
    { .name = "price",   .label = "Price",   .type = COL_NUM,  .nullable = true },
    { .name = "currency",.label = "Currency",.type = COL_TEXT, .nullable = true },
};

Crud_Module crud_module_ill_types = {
    .path         = "/ill-types",
    .title        = "Illness Category",
    .table        = "IllTypes",
    .columns      = ill_type_columns,
    .column_count = ARRAY_LEN(ill_type_columns),
};

Crud_Module crud_module_ill_prices = {
    .path         = "/ill-prices",
    .title        = "Ill Price",
    .table        = "IllPrices",
    .columns      = ill_price_columns,
    .column_count = ARRAY_LEN(ill_price_columns),
};

Crud_Module crud_module_departments = {
    .path         = "/departments",
    .title        = "Department",
    .table        = "Departments",
    .columns      = department_columns,
    .column_count = ARRAY_LEN(department_columns),
};

Crud_Module crud_module_doctors = {
    .path         = "/doctors",
    .title        = "Doctor",
    .table        = "Doctors",
    .columns      = doctor_columns,
    .column_count = ARRAY_LEN(doctor_columns),
};


Crud_Module crud_module_doctor_work_times = {
    .path         = "/doctor-work-times",
    .title        = "Doctor Work Time",
    .table        = "DoctorWorkTimes",
    .columns      = doctor_work_time_columns,
    .column_count = ARRAY_LEN(doctor_work_time_columns),
};

Crud_Module crud_module_suppliers = {
    .path         = "/suppliers",
    .title        = "Medicine Supplier",
    .table        = "Supplier",
    .columns      = supplier_columns,
    .column_count = ARRAY_LEN(supplier_columns),
};

Crud_Module crud_module_medicine_types = {
    .path         = "/medicine-types",
    .title        = "Medicine Category",
    .table        = "MedicineTypes",
    .columns      = medicine_type_columns,
    .column_count = ARRAY_LEN(medicine_type_columns),
};

Crud_Module crud_module_medicines = {
    .path         = "/medicines",
    .title        = "Medicine",
    .table        = "Medicines",
    .columns      = medicine_columns,
    .column_count = ARRAY_LEN(medicine_columns),
};

Crud_Module crud_module_patients = {
    .path         = "/patients",
    .title        = "Patient",
    .table        = "Patients",
    .columns      = patient_columns,
    .column_count = ARRAY_LEN(patient_columns),
};

Crud_Module crud_module_patient_daily_invoices = {
    .path         = "/patient-daily-invoices",
    .title        = "Patient Daily Invoice",
    .table        = "PatientDailyInvoices",
    .columns      = patient_daily_invoice_columns,
    .column_count = ARRAY_LEN(patient_daily_invoice_columns),
};

Crud_Module crud_module_patient_daily_invoice_details = {
    .pk_name      = "rowid",
    .path         = "/patient-daily-invoices-details",
    .title        = "Daily Invoice Detail",
    .table        = "PatientDailyInvoicesDetails",
    .columns      = patient_daily_invoice_detail_columns,
    .column_count = ARRAY_LEN(patient_daily_invoice_detail_columns),
};

Crud_Module crud_module_patient_medicine_invoices = {
    .path         = "/patient-medicine-invoices",
    .title        = "Patient Medicine Invoice",
    .table        = "PatientMedicineInvoices",
    .columns      = patient_medicine_invoice_columns,
    .column_count = ARRAY_LEN(patient_medicine_invoice_columns),
};

Crud_Module crud_module_patient_medicine_invoice_details = {
    .pk_name      = "rowid",
    .path         = "/patient-medicine-invoices-details",
    .title        = "Medicine Invoice Detail",
    .table        = "PatientMedicineInvoiceDetails",
    .columns      = patient_medicine_invoice_detail_columns,
    .column_count = ARRAY_LEN(patient_medicine_invoice_detail_columns),
};

Crud_Module crud_module_patient_invoice_out_details = {
    .pk_name      = "rowid",
    .path         = "/patient-invoice-out-details",
    .title        = "Room Invoice Detail",
    .table        = "PatientInvoiceOutDetails",
    .columns      = patient_invoice_out_detail_columns,
    .column_count = ARRAY_LEN(patient_invoice_out_detail_columns),
};

Crud_Module crud_module_org_daily_invoice_details = {
    .pk_name      = "rowid",
    .path         = "/organization-daily-invoices-details",
    .title        = "Org Daily Invoice Detail",
    .table        = "OrganizationDailyInvoiceDetails",
    .columns      = org_daily_invoice_detail_columns,
    .column_count = ARRAY_LEN(org_daily_invoice_detail_columns),
};

Crud_Module crud_module_org_invoice_out_details = {
    .pk_name      = "rowid",
    .path         = "/organization-invoice-out-details",
    .title        = "Org Invoice Out Detail",
    .table        = "OrganizationInvoiceOutDetails",
    .columns      = org_invoice_out_detail_columns,
    .column_count = ARRAY_LEN(org_invoice_out_detail_columns),
};

Crud_Module crud_module_org_invoice_details = {
    .pk_name      = "rowid",
    .path         = "/organization-invoice-details",
    .title        = "Org Invoice Detail",
    .table        = "OrganizationInvoiceDetail",
    .columns      = org_invoice_detail_columns,
    .column_count = ARRAY_LEN(org_invoice_detail_columns),
};

Crud_Module crud_module_org_balances = {
    .pk_name      = "rowid",
    .path         = "/organization-balances",
    .title        = "Organization Balance",
    .table        = "OrganizationBalance",
    .columns      = org_balance_columns,
    .column_count = ARRAY_LEN(org_balance_columns),
};

Crud_Module crud_module_appointments = {
    .path         = "/appointments",
    .title        = "Appointment",
    .table        = "Appointment",
    .columns      = appointment_columns,
    .column_count = ARRAY_LEN(appointment_columns),
};

Crud_Module crud_module_room_types = {
    .path         = "/room-types",
    .title        = "Room Type",
    .table        = "RoomTypes",
    .columns      = room_type_columns,
    .column_count = ARRAY_LEN(room_type_columns),
};

Crud_Module crud_module_rooms = {
    .path         = "/rooms",
    .title        = "Room",
    .table        = "Rooms",
    .columns      = room_columns,
    .column_count = ARRAY_LEN(room_columns),
};

Crud_Module crud_module_organizations = {
    .path         = "/organizations",
    .title        = "Organization",
    .table        = "Organizations",
    .columns      = organization_columns,
    .column_count = ARRAY_LEN(organization_columns),
};

Crud_Module crud_module_org_invoices = {
    .path         = "/organization-invoices",
    .title        = "Organization Invoice",
    .table        = "OrganizationInvoices",
    .columns      = org_invoice_columns,
    .column_count = ARRAY_LEN(org_invoice_columns),
};

Crud_Module crud_module_org_payments = {
    .path         = "/organization-payments",
    .title        = "Organization Payment",
    .table        = "OrganizationPayment",
    .columns      = org_payment_columns,
    .column_count = ARRAY_LEN(org_payment_columns),
};

Crud_Module *crud_modules[] = {
    &crud_module_ill_types,
    &crud_module_ill_prices,
    &crud_module_departments,
    &crud_module_doctors,
    &crud_module_doctor_work_times,
    &crud_module_suppliers,
    &crud_module_medicine_types,
    &crud_module_medicines,
    &crud_module_patients,
    &crud_module_patient_daily_invoices,
    &crud_module_patient_daily_invoice_details,
    &crud_module_patient_medicine_invoices,
    &crud_module_patient_medicine_invoice_details,
    &crud_module_patient_invoice_out_details,
    &crud_module_org_daily_invoice_details,
    &crud_module_org_invoice_out_details,
    &crud_module_org_balances,
    &crud_module_org_invoice_details,
    &crud_module_org_invoice_out_details,
    &crud_module_appointments,
    &crud_module_room_types,
    &crud_module_rooms,
    &crud_module_organizations,
    &crud_module_org_invoices,
    &crud_module_org_invoice_details,
    &crud_module_org_payments,
};

const size_t crud_modules_count = ARRAY_LEN(crud_modules);

